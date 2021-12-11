#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <poplar/Engine.hpp>
#include <poplar/Graph.hpp>
#include <popops/ElementWise.hpp>
#include <popops/codelets.hpp>
#include <popops/Reduce.hpp>
#include <popops/Cast.hpp>

#include "common.hpp"

using namespace std;
using namespace poplar;
using namespace poplar::program;


int main()
{

  // 
  // First read in the data and put it into to vector of ints
  //

  // Read in the data
  ifstream data("data.txt");

  // Use a while loop together with the getline() function to read the file line by line
  // And write to a matrix of row x columns
  string line;
  std::vector<std::vector<int>> values;
  while (getline(data, line))
  {
    std::vector<int> r;
    for(char c : line) {
      r.push_back(c == '0' ? 0 : 1);
    }
    values.push_back(r);
  }

  // This vector will hold the result
  auto result = std::vector<int>(1);

  //
  // Workout the size of the matrix
  //
  auto numRows = values.size();
  auto numCols = values[0].size(); 

  //
  // Create a flatten list of values so we can write the data onto the IPU
  //
  std::vector<int> flattenValues;
  for(int i = 0; i < values.size(); ++i) {
    for(int j = 0; j < values[i].size(); ++j) {
      flattenValues.push_back(values[i][j]);
    }
  }


  //
  // Get an IPU Device, Target & Graph
  //
  auto device = GetIPUDevice();
  Target target = device.getTarget();
  Graph graph(target);
  popops::addCodelets(graph);

  // 
  // Create a tensors on the IPU to receive the input data and map it evenly over
  // the tiles.
  //

  Tensor inputTensor = graph.addVariable(INT, {numRows, numCols}, "inputTensor");
  
  for (unsigned i = 0; i < numRows ; ++i) {
    for (unsigned j = 0; j < numCols ; ++j) {
      graph.setTileMapping(inputTensor[i][j], i);
    }
  }

  //
  // Create some constants we will use later, 1, 0 and a list of powers of two
  //
  Tensor oneTensor = graph.addConstant<int>(INT, {1}, {1}, "one");
  graph.setTileMapping(oneTensor, 0);

  Tensor zero = graph.addConstant<int>(INT, {1}, {0}, "zero");
  graph.setTileMapping(zero, 0);

  Tensor powersOfTwoTensor = graph.addConstant<int>(INT, {12}, {2048, 1024, 512, 256, 128, 64, 32, 16, 8, 4, 2, 1}, "powersOfTwo");
  graph.setTileMapping(powersOfTwoTensor, 0);

  //
  // Reshape the powers of two tensor to be the same as the number of columns
  //
  Tensor powersOfTwoTruncate = powersOfTwoTensor.slice(powersOfTwoTensor.numElements() - numCols, powersOfTwoTensor.numElements(), 0);


  //
  // Create the a poplar program
  //
  Sequence algorithm;

  //
  // First we are going to convert the input matrix so we have -1 instead of 0. This will enable me to reduce
  // the colum values and work out if there are more 1' or 0's. i.e the result will be > 0 if more 1 or < 0 if
  // more 0's
  // 
  // Turn
  // { 
  //   {0, 1, 0}
  //   {1, 1, 0}
  //   {1, 0, 1}
  // }
  //
  // Into
  // { 
  //   {-1,  1, -1}
  //   { 1,  1, -1}
  //   { 1, -1,  1}
  // }


  Tensor negInputTensor = popops::sub(graph, inputTensor, oneTensor, algorithm, "Subtract");
  Tensor posNegTensor = popops::add(graph, inputTensor, negInputTensor, algorithm, "Add");

  //
  // Then reduce all the columns to get a positive or negative value
  //
  // Turn
  // { 
  //   {-1,  1, -1}
  //   { 1,  1, -1}
  //   { 1, -1,  1}
  // }
  //
  // Into 
  // {
  //   { 1,  1, -1}
  // }
  Tensor totalTensor = popops::reduce(graph, posNegTensor, INT, {0}, {popops::Operation::ADD}, algorithm, "ColumnReduction");

  //
  // Convert the totals into which are greater or less than 0. Need to cast
  // the output to INT as the following operations don't work on BOOL
  //
  // Turn
  // { 
  //   { 1,  1, -1}
  // }
  //
  // Into 
  // {
  //   { 1,  1,  0}
  // }  
  Tensor bitTensor = popops::gt(graph, totalTensor, zero, algorithm, "GreaterThan0");
  Tensor bitCastTensor = popops::cast(graph, bitTensor, INT, algorithm, "GreaterThan0Cast");

  // 
  // Now we have a bit map, we can multiple it by powers of two to get the values for each power and then number them 
  // to get the total. First we do gamma. And will use PrintTensor to output the value.
  //
  Tensor gammaPartsTensor = popops::mul(graph, bitCastTensor, powersOfTwoTruncate, algorithm, "CalculateGammaPart");
  Tensor gammaTensor = popops::reduce(graph, gammaPartsTensor, INT, {0}, {popops::Operation::ADD}, algorithm, "CalculateGamma");
  algorithm.add(PrintTensor("gamma is = ", gammaTensor));  

  // 
  // For epsilon we do the same, but we first need to invert the bitmap. To invert the bitmap we will subtract 1 and
  // then square the value
  //
  // Turn              { 1, 1, 0}
  // Into (subtract 1) { 0, 0, -1}
  // Then (square)     { 0, 0, 1}
  //
  // Note: Using the popops::map function to compute the inverted bitmap in 1 api
  Tensor invertTensor = popops::map(graph, 
                                    popops::expr::Square(
                                      popops::expr::Sub(popops::expr::_1, popops::expr::_2)), 
                                    {bitCastTensor, oneTensor}, algorithm, "InvertBitmap");
  Tensor epsilonPartsTensor = popops::mul(graph, invertTensor, powersOfTwoTruncate, algorithm, "CalculateEpsilonParts");
  Tensor epsilon = popops::reduce(graph, epsilonPartsTensor, INT, {0}, {popops::Operation::ADD}, algorithm, "CalculateEpsilon");
  algorithm.add(PrintTensor("epsilon is = ", epsilon));  

  // 
  // Finally multiple epsilon and gamma together.
  //
  Tensor resultTensor = popops::mul(graph, gammaTensor, epsilon,  algorithm, "Multiply");

  //
  // Set up data streams to copy data in and out of graph
  //
  auto inputStream = graph.addHostToDeviceFIFO("data", INT, numRows * numCols);
  auto outputStream = graph.addDeviceToHostFIFO("result", INT, 1);
  
  //
  // Create top level program which copies data onto the IPU, run the algorithm and copies the data of the ipu
  //
  auto toplevelProg = Sequence({Copy(inputStream, inputTensor.flatten()), 
                               algorithm,
                               Copy(resultTensor, outputStream)});

  // 
  // Create the engine and run the program
  //
  Engine engine(graph, toplevelProg);
  engine.load(device);

  // 
  // Connect the streams to the data on the host
  //
  engine.connectStream("data", flattenValues.data());
  engine.connectStream("result", result.data());

  //
  // Run the program
  //
  engine.run(0);
  
  //
  // Print the result
  //
  std::cout << "Result = " << result[0] << endl;
  
  return 0;
}
