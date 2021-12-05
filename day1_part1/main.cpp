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
  // Create a tensor on the IPU to receive the input data and map it evenly over
  // the tiles. As we have 2000 measuments we will put two measurements on each tile
  //

  Tensor inputDataTensor = graph.addVariable(INT, {numMeasurements}, "inputData");
  for (unsigned i = 0; i < numMeasurements / 2; ++i)
  {
    graph.setTileMapping(inputDataTensor[(i * 2)], i);
    graph.setTileMapping(inputDataTensor[(i * 2) + 1], i);
  }

  //
  // Create a second tensor which is the same as inputData but offset 
  // The first element being repeated.
  //
  Tensor inputDataOffsetTensor = concat(inputDataTensor.slice(0, 1, 0), inputDataTensor.slice(0, numMeasurements - 1, 0));

  //
  // Create a zero constant tensor
  //
  Tensor zero = graph.addConstant<int>(INT, {1}, {0}, "zero");
  graph.setTileMapping(zero, 0);

  //
  // Create the a poplar program
  Sequence algorithm;

  //
  // Subtract the input from the input offset tensor to calcualte the different
  //
  Tensor differenceTensor = popops::sub(graph, inputDataTensor, inputDataOffsetTensor, algorithm, "Subtract");

  //
  // Determine which values are greater than zero
  //
  Tensor greaterThanZeroTensor = popops::gt(graph, differenceTensor, zero, algorithm, "Greater");

  //
  // Need to cast to an INT as the reduce does not work with BOOL
  //
  Tensor greaterThanZeroCastTensor = popops::cast(graph, greaterThanZeroTensor, INT, algorithm, "Cast");

  //
  // Count the number of 1's using a reduce
  //
  Tensor resultTensor = popops::reduce(graph, greaterThanZeroCastTensor, INT, {0}, {popops::Operation::ADD}, algorithm, "Reduction");

  //
  // Set up data streams to copy data in and out of graph
  //
  auto inputStream = graph.addHostToDeviceFIFO("data", INT, numMeasurements);
  auto outputStream = graph.addDeviceToHostFIFO("result", INT, 1);

  //
  // Create top level program which copies data onto the IPU, run the algorithm and copies the data of the ipu
  //
  auto toplevelProg = Sequence(Copy(inputStream, inputDataTensor), algorithm, Copy(resultTensor, outputStream));

  // 
  // Create the engine and run the program
  //
  Engine engine(graph, toplevelProg);
  engine.load(device);

  // 
  // Connect the streams to the data on the host
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
