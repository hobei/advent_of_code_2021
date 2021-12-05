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
  vector<int> aims;
  vector<int> vValues;

  int aim = 0;
  while (getline(data, line))
  {
    // Output the text from the file
    auto space = line.find(" ");
    auto command = line.substr(0, space);
    auto value = line.substr(space + 1, line.size() - space);

    if(command == "forward") {
      hValues.push_back(atoi(value.c_str()));
      aims.push_back(aim);
    } else if (command == "up") {
      aim = aim + (0 - atoi(value.c_str()));
      //vValues.push_back(0 - atoi(value.c_str()));
    } else if (command == "down") {
      aim = aim + atoi(value.c_str());
      //vValues.push_back(atoi(value.c_str()));
    }
  }
  // This vector will hold the result
  auto result = std::vector<int>(1);



  //
  // Workout the number of elements in the list
  //
  auto numHCmds = hValues.size();
  auto numAims = aims.size();
  cout << "Number of horizontal commands = " << numHCmds << endl;
  cout << "Number of aim commands = " << numAims << endl;



  //
  // Get an IPU Device, Target & Graph
  //
  auto device = GetIPUDevice();
  Target target = device.getTarget();
  Graph graph(target);
  popops::addCodelets(graph);

  // 
  // Create a tensors on the IPU to receive the input data and map it evenly over
  // the tiles. Of shape {numHCmds, 2}. The first column is going to be hCommands
  // The second column is going to be the aim value.
  //

  Tensor inputTensor = graph.addVariable(INT, {numHCmds, 2}, "inputTensor");
  for (unsigned i = 0; i < numHCmds ; ++i)
  {
    graph.setTileMapping(inputTensor[i][0], i);
    graph.setTileMapping(inputTensor[i][1], i);
  }


  //
  // Create the a poplar program
  //
  Sequence algorithm;

  //
  // Sum the horizontal commands
  //
  Tensor horizontalTensor = popops::reduce(graph, inputTensor.slice(0, 1, 1), INT, {0}, {popops::Operation::ADD}, algorithm, "ReductionH");

  //
  // Calculate the depth. 
  // - First multiple the forward by the aim fo each row
  // - Then sum the resulting values
  //
  Tensor depthTensor = popops::reduce(graph, inputTensor, INT, {1}, {popops::Operation::MUL}, algorithm, "ReductionDepth");
  Tensor depthSumTensor = popops::reduce(graph, depthTensor, INT, {0}, {popops::Operation::ADD}, algorithm, "ReductionDepthSum");

  //
  // Multiply the results
  //
  Tensor resultTensor = popops::mul(graph, horizontalTensor, depthSumTensor, algorithm, "Multiplication");

  //
  // Set up data streams to copy data in and out of graph
  //
  auto inputHStream = graph.addHostToDeviceFIFO("dataH", INT, numHCmds);
  auto inputAStream = graph.addHostToDeviceFIFO("dataA", INT, numAims);
  auto outputStream = graph.addDeviceToHostFIFO("result", INT, 1);
  
  //
  // Create top level program which copies data onto the IPU, run the algorithm and copies the data of the ipu
  // We are going to copy the forward values into the first column and the aim values in the second
  //
  auto toplevelProg = Sequence(Copy(inputHStream, inputTensor.slice(0, 1, 1).flatten()), 
                               Copy(inputAStream, inputTensor.slice(1, 2, 1).flatten()),
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
  engine.connectStream("dataA", aims.data());
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
