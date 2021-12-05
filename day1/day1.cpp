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

using namespace std;
using namespace poplar;
using namespace poplar::program;

int main()
{

  // 1. First read in the data and put it into to vector of ints

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

  // 2. Workout the number of elements in the list
  auto numMeasurements = values.size();
  cout << "Number of measurements = " << numMeasurements << endl;

  // 3. Create a vector of 0's.
  vector<int> zeros(numMeasurements);
  std::fill(zeros.begin(), zeros.end(), 0);

  // 4. Initialise the IPU, Target and Graph
  // Create the IPU model device
  IPUModel ipuModel;
  Device device = ipuModel.createDevice();
  Target target = device.getTarget();

  // Create the Graph object
  Graph graph(target);
  popops::addCodelets(graph);

  // 5. Create variables for the original data, the offset data, and the zeros'
  //    And set the tile mapping so 2 measurements are on each tile
  // Add variables to the graph
  Tensor v1 = graph.addVariable(INT, {numMeasurements}, "v1");
  Tensor v2 = graph.addVariable(INT, {numMeasurements}, "v2");
  Tensor v3 = graph.addVariable(INT, {numMeasurements}, "v3");
  for (unsigned i = 0; i < numMeasurements / 2; ++i)
  {
    graph.setTileMapping(v1[(i * 2)], i);
    graph.setTileMapping(v1[(i * 2) + 1], i);

    graph.setTileMapping(v2[(i * 2)], i);
    graph.setTileMapping(v2[(i * 2) + 1], i);

    graph.setTileMapping(v3[(i * 2)], i);
    graph.setTileMapping(v3[(i * 2) + 1], i);
  }

  // 6. Create constants for the data, the offset data and zeros
  // Create a control program that is a sequence of steps
  Sequence prog;

  Tensor c0 = graph.addConstant<int>(INT, {numMeasurements}, zeros, "const_c0");
  // Add steps to initialize the variables
  Tensor c1 = graph.addConstant<int>(INT, {numMeasurements}, values, "const_c1");

  // Use std::rotate to offset the second data
  auto values2 = values;
  std::rotate(values2.rbegin(), values2.rbegin() + 1, values2.rend());
  // Need to set the first value of the second data to the same as the first
  values2[0] = values[0];

  Tensor c2 = graph.addConstant<int>(INT, {numMeasurements}, values2, "const_c2");

  // Map the constants to the tiles. 
  graph.setTileMapping(c0, 0);
  graph.setTileMapping(c1, 0);
  graph.setTileMapping(c2, 0);

  // Copy the data to the variables
  prog.add(Copy(c1, v1.flatten()));
  prog.add(Copy(c2, v2.flatten()));

  // 7. Subtract the data the off-set data
  Tensor v4 = popops::sub(graph, v1, v2, prog, "Subtract");

  // 8. Determine which values are greater than zero
  Tensor v5 = popops::gt(graph, v4, c0, prog, "Greater");

  // Need to cast to an INT as the reduce does not work with Bool.
  Tensor v6 = popops::cast(graph, v5, INT, prog, "Cast");

  // 9. Count the number of 1's using a reduce
  Tensor v7 = popops::reduce(graph, v6, INT, {0}, {popops::Operation::ADD}, prog, "Reduction");

  // 10. Print the result
  prog.add(PrintTensor("Num increaseing measurements", v7));

  // 11. Create the engine and run the program
  // Create the engine
  Engine engine(graph, prog);
  engine.load(device);

  // Run the control program
  engine.run(0);

  return 0;
}
