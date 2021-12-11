#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <poplar/Engine.hpp>
#include <poplar/Graph.hpp>
#include <poplar/IPUModel.hpp>
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
  string line;
  vector<int> values;
  while (getline(data, line))
  {
    // Output the text from the file
    values.push_back(atoi(line.c_str()));
  }

  // This vector will hold the result
  auto result = std::vector<int>(1);

  //
  // Workout the number of elements in the list
  //
  auto numMeasurements = values.size();
  cout << "Number of measurements = " << numMeasurements << endl;

  //
  // Get an IPU Device, Target & Graph
  //
  auto device = GetIPUDevice();
  Target target = device.getTarget();
  Graph graph(target);
  popops::addCodelets(graph);

  //
  // Create tensor on the IPU to receive the data an map it over the tiles.
  // We will create a tensor fo shape {numMeasurements, 3} so we can sum 3 values 
  // using a reduction
  //

  Tensor inputDataTensor = graph.addVariable(INT, {numMeasurements, 3}, "inputData");
  for (unsigned i = 0; i < 3; ++i) {
    for (unsigned j = 0; j < numMeasurements / 2; ++j) {
      graph.setTileMapping(inputDataTensor[(j * 2)][i], j);
      graph.setTileMapping(inputDataTensor[(j * 2) + 1][i], j);  
    }
  }

  Tensor zero = graph.addConstant<int>(INT, {1}, {0}, "zero");
  graph.setTileMapping(zero, 0);

  // 6. Create constants for the data, the offset data and zeros
  // Create a control program that is a sequence of steps
  Sequence prog;

  // Helper tensor of the first column of the input date tensor
  Tensor inputDataColOneTensor = inputDataTensor.slice(0, 1, 1).flatten();

  //
  // Copy and rotate the values into the other columns
  //
  // We want to end up with a matrix like this
  // 
  //  A B C
  //  B C D 
  //  C D 0
  //  D 0 0
  //
  prog.add(Copy(concat(inputDataColOneTensor.slice(1, numMeasurements, 0), zero), inputDataTensor.slice(1, 2, 1).flatten()));
  prog.add(Copy(concat(inputDataColOneTensor.slice(2, numMeasurements, 0), zero.broadcast(2, 0)), inputDataTensor.slice(2, 3, 1).flatten()));

  //
  // Sum the row i.e. reduce in first column
  //
  // i.e.
  // 
  //  A B C = A + B + C
  //  B C D = B + C + D
  //  C D 0 = C + D
  //  D 0 0 = D
  //
  Tensor inputWindowedDataTensor = popops::reduce(graph, inputDataTensor, INT, {1}, {popops::Operation::ADD}, prog, "Reduction");

  //
  // Create a second tensor which is the same as inputData but offset 
  // The first element being repeated.
  //
  Tensor inputWindowedDataOffsetTensor = concat(inputWindowedDataTensor.slice(0, 1, 0), inputWindowedDataTensor.slice(0, numMeasurements -1, 0));

  //
  // Subtract the input from the input offset tensor to calcualte the different
  //
  Tensor differenceTensor = popops::sub(graph, inputWindowedDataTensor, inputWindowedDataOffsetTensor, prog, "Subtract");

  //
  // Determine which values are greater than zero
  //
  Tensor greaterThanZeroTensor = popops::gt(graph, differenceTensor, zero, prog, "Greater");

  //
  // Need to cast to an INT as the reduce does not work with BOOL
  //
  Tensor greaterThanZeroCastTensor = popops::cast(graph, greaterThanZeroTensor, INT, prog, "Cast");

  //
  // Count the number of 1's using a reduce
  //
  Tensor resultTensor = popops::reduce(graph, greaterThanZeroCastTensor, INT, {0}, {popops::Operation::ADD}, prog, "Reduction");

  //
  // Set up data streams to copy data in and out of graph
  //
  auto inputStream = graph.addHostToDeviceFIFO("data", INT, numMeasurements);
  auto outputStream = graph.addDeviceToHostFIFO("result", INT, 1);

  //
  // Create top level program which copies data onto the IPU, run the algorithm and copies the data of the ipu
  //
  auto toplevelProg = Sequence({Copy(inputStream, inputDataTensor.slice(0, 1, 1).flatten()), prog, Copy(resultTensor, outputStream)});

  // 
  // Create the engine and run the program
  //
  Engine engine(graph, toplevelProg);
  engine.load(device);

  // 
  // Connect the streams to the data on the host
  //
  engine.connectStream("data", values.data());
  engine.connectStream("result", result.data());

  //
  // Run the program
  //
  engine.run(0);

  //
  // Print the result
  //
  std::cout << "Num increasing measurements = " << result[0] << endl;

  return 0;
}
