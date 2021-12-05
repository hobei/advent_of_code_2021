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
  //vector<unsigned> commands;
  vector<int> hValues;
  vector<int> vValues;
  while (getline(data, line))
  {
    // Output the text from the file
    auto space = line.find(" ");
    auto command = line.substr(0, space);
    auto value = line.substr(space + 1, line.size() - space);

    if(command == "forward") {
      hValues.push_back(atoi(value.c_str()));
    } else if (command == "up") {
      vValues.push_back(0 - atoi(value.c_str()));
    } else if (command == "down") {
      vValues.push_back(atoi(value.c_str()));
    }
  }
  // This vector will hold the result
  auto result = std::vector<int>(1);

  //
  // Workout the number of elements in the list
  //
  auto numHCmds = hValues.size();
  auto numVCmds = vValues.size();
  cout << "Number of horizontal commands = " << numHCmds << endl;
  cout << "Number of depth commands = " << numVCmds << endl;

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

  Tensor inputHCommandsTensor = graph.addVariable(INT, {numHCmds}, "inputHCommands");
  for (unsigned i = 0; i < numHCmds ; ++i)
  {
    graph.setTileMapping(inputHCommandsTensor[i], i);
  }

  Tensor inputVCommandsTensor = graph.addVariable(INT, {numVCmds}, "inputVCommands");
  for (unsigned i = 0; i < numVCmds ; ++i)
  {
    graph.setTileMapping(inputVCommandsTensor[i], i);
  }

  //
  // Create the a poplar program
  //
  Sequence algorithm;

  //
  // Sum the horizontal commands
  //
  Tensor resultHTensor = popops::reduce(graph, inputHCommandsTensor, INT, {0}, {popops::Operation::ADD}, algorithm, "ReductionH");

  //
  // Sum the vertical commands
  //
  Tensor resultVTensor = popops::reduce(graph, inputVCommandsTensor, INT, {0}, {popops::Operation::ADD}, algorithm, "ReductionV");

  //
  // Multiply the results
  //
  Tensor resultTensor = popops::mul(graph, resultHTensor, resultVTensor, algorithm, "Multiplication");

  //
  // Set up data streams to copy data in and out of graph
  //
  auto inputHStream = graph.addHostToDeviceFIFO("dataH", INT, numHCmds);
  auto inputVStream = graph.addHostToDeviceFIFO("dataV", INT, numVCmds);
  auto outputStream = graph.addDeviceToHostFIFO("result", INT, 1);
  
  //
  // Create top level program which copies data onto the IPU, run the algorithm and copies the data of the ipu
  //
  auto toplevelProg = Sequence(Copy(inputHStream, inputHCommandsTensor), 
                               Copy(inputVStream, inputVCommandsTensor), 
                               algorithm, 
                               Copy(resultTensor, outputStream));

  // 
  // Create the engine and run the program
  //
  Engine engine(graph, toplevelProg);
  engine.load(device);

  // 
  // Connect the streams to the data on the host
  //
  engine.connectStream("dataH", hValues.data());
  engine.connectStream("dataV", vValues.data());
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
