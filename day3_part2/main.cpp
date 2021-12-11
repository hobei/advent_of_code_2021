#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <utility>
#include <poplar/Engine.hpp>
#include <poplar/Graph.hpp>
#include <popops/ElementWise.hpp>
#include <popops/codelets.hpp>
#include <popops/Reduce.hpp>
#include <popops/Cast.hpp>
#include <popops/TopK.hpp>
#include <popops/DynamicSlice.hpp>

#include "common.hpp"

using namespace std;
using namespace poplar;
using namespace poplar::program;


Tensor invert(Graph& graph, Tensor a, Sequence& prog) {
  Tensor oneTensor = graph.addConstant<int>(INT, {1}, {1}, "one");
  graph.setTileMapping(oneTensor, 0);

  Tensor i = popops::sub(graph, a, oneTensor, prog, "");
  Tensor result = popops::abs(graph, i, prog, "");
  return result;
}

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

  cout << "NumRow = " << numRows << " NumCols = " << numCols << endl;

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
  Tensor mask = graph.addVariable(INT, {numRows, numCols}, "mask");
  
  for (unsigned i = 0; i < numRows ; ++i) {
    for (unsigned j = 0; j < numCols ; ++j) {
      graph.setTileMapping(inputTensor[i][j], i);
      graph.setTileMapping(mask[i][j], i);
    }
  }

  //
  // Create a counter variable that will be used to slice the input columns
  //
  Tensor counter = graph.addVariable(UNSIGNED_INT, {1}, "counter");
  graph.setTileMapping(counter, 0);

  //
  // Create a counter that will accumulate the number of 0's which are for rows
  // that we have filtered out.
  //
  Tensor num0Counter = graph.addVariable(INT, {1}, "num0Counter");
  graph.setTileMapping(num0Counter, 0);

  //
  // Create some constants we will use later, 1, 0 and a list of powers of two
  //
  Tensor oneTensor = graph.addConstant<int>(INT, {1}, {1}, "one");
  graph.setTileMapping(oneTensor, 0);
 
  Tensor oneTensorU = graph.addConstant<unsigned>(UNSIGNED_INT, {1}, {1}, "one");
  graph.setTileMapping(oneTensorU, 0);

  Tensor trueTensorC = graph.addConstant<bool>(BOOL, {1}, {true}, "true");
  graph.setTileMapping(trueTensorC, 0);  

  Tensor falseTensorC = graph.addConstant<bool>(BOOL, {1}, {false}, "false");
  graph.setTileMapping(falseTensorC, 0);  

  Tensor loopPredicate = graph.addVariable(BOOL, {1}, "loopPredicate");
  graph.setTileMapping(loopPredicate, 0);

  Tensor zero = graph.addConstant<int>(INT, {1}, {0}, "zero");
  graph.setTileMapping(zero, 0);

  Tensor zeroU = graph.addConstant<unsigned>(UNSIGNED_INT, {1}, {0U}, "zero");
  graph.setTileMapping(zeroU, 0U);

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


  // Calculate the Oxygen Generator Rating
  Tensor ogrTensor;
  
  {
    // 
    // Copy the input as we are going to apply inplace operations on it
    //
    Tensor inputCopyTensor = graph.clone(inputTensor);
    algorithm.add(Copy(inputTensor, inputCopyTensor));
    
    //
    // Initialize the tensors
    //
    algorithm.add(Copy(trueTensorC, loopPredicate));
    algorithm.add(Copy(zeroU, counter));
    algorithm.add(Copy(zero, num0Counter));
    
    //
    // The loop sequence
    //
    Sequence loop;
   
    //
    // Get the column
    //
    Tensor columnTensor = popops::dynamicSlice(graph, inputCopyTensor, counter, {1}, {1}, loop, "ExtractColumn");
    
    // Determine the number of 1's
    Tensor num1sInColumnTensor = popops::reduce(graph, columnTensor, INT, {0}, {popops::Operation::ADD}, loop, "Count1s");
        
    // Determine the number of 0's
    // We will count the number of 0's by subtracting 1, summing and then taking the abs value.
    Tensor colMinusOne = popops::sub(graph, columnTensor, oneTensor, loop);
    Tensor num0sNegative = popops::reduce(graph, colMinusOne, INT, {0}, {popops::Operation::ADD}, loop, "");
    Tensor num0sTotal= popops::abs(graph, num0sNegative, loop, "");

    // Then we will subtract the running count of the number of filtered out rows.
    Tensor num0sInColumnTensor = popops::sub(graph, num0sTotal, num0Counter , loop, "Count0's");

    // Determine the number of readings in this loop
    Tensor numReadings = popops::add(graph, num1sInColumnTensor, num0sInColumnTensor, loop, "NumReadings");

    // Calculate the predicate for the mask
    Tensor more1sPredicate = popops::gteq(graph, num1sInColumnTensor, num0sInColumnTensor, loop, "Predicate").reshape({});

    // Sequences for the if/else below
    Sequence oneBody;
    Sequence zeroBody;

    // Mask if more 1's
    popops::addInPlace(graph, num0Counter, num0sInColumnTensor, oneBody);
    oneBody.add(Copy(columnTensor.broadcast(numCols, 1), mask));

    // Mask if more 0's
    Tensor maskInvert = invert(graph, columnTensor.broadcast(numCols, 1), zeroBody);
    popops::addInPlace(graph, num0Counter, num1sInColumnTensor, zeroBody);
    zeroBody.add(Copy(maskInvert, mask));

    // If statement
    loop.add(If(more1sPredicate, oneBody, zeroBody, "IfMore1s"));
    
    // Need to reshape to make scalar
    Tensor moreThan1Reading = popops::gt(graph, numReadings, oneTensor, loop, "Predicate").reshape({});
    loop.add(Copy(moreThan1Reading, loopPredicate));
    
    // Increase the counter
    popops::addInPlace(graph, counter, oneTensorU, loop);
    
    // Apply the mask
    popops::mulInPlace(graph, inputCopyTensor, mask, loop);
    
    //
    // For each column 
    // 
    algorithm.add(RepeatWhileTrue(Sequence(), loopPredicate.reshape({}), loop, "Repeat"));
    
    // 
    // Reduce the rows to be left with the resulting row
    //
    Tensor finalBitmap = popops::reduce(graph, inputCopyTensor, INT, {0}, {popops::Operation::ADD}, algorithm, "ColumnReduction");
    algorithm.add(PrintTensor("Oxygen Generator Rating (Bitmap) =", finalBitmap));

    Tensor ogrTensorParts = popops::mul(graph, finalBitmap, powersOfTwoTruncate, algorithm, "CalculateOGRatePart");
    ogrTensor = popops::reduce(graph, ogrTensorParts, INT, {0}, {popops::Operation::ADD}, algorithm, "CalculateOGRate");
    algorithm.add(PrintTensor("Oxygen Generator Rating is = ", ogrTensor));  
    
  }
  
  // Calculate the CO2 scrubber rating
  Tensor co2SrTensor;
  {
    // 
    // Copy the input as we are going to apply inplace operations on it
    //
    Tensor inputCopyTensor = graph.clone(inputTensor);
    algorithm.add(Copy(inputTensor, inputCopyTensor));
    
    //
    // Initialize the Tensors
    //
    algorithm.add(Copy(trueTensorC, loopPredicate));
    algorithm.add(Copy(zeroU, counter));
    algorithm.add(Copy(zero, num0Counter));

    Sequence loop;
   
    // Get the column
    Tensor columnTensor = popops::dynamicSlice(graph, inputCopyTensor, counter, {1}, {1}, loop, "");
    
    // Determine the number of 1's
    Tensor num1sInColumnTensor = popops::reduce(graph, columnTensor, INT, {0}, {popops::Operation::ADD}, loop, "");
        
    // Determine the number of 0's
    Tensor colMinusOne = popops::sub(graph, columnTensor, oneTensor, loop);
    Tensor num0s = popops::reduce(graph, colMinusOne, INT, {0}, {popops::Operation::ADD}, loop, "");
    Tensor num0sTotal= popops::abs(graph, num0s, loop, "");
    Tensor num0sInColumnTensor = popops::sub(graph, num0sTotal, num0Counter , loop, "");

    Tensor numReadings = popops::add(graph, num1sInColumnTensor, num0sInColumnTensor, loop);

    // This is diffent we we need to stop when there is only 1 reading left
    Tensor moreReadings = popops::gt(graph, numReadings, oneTensor, loop).reshape({});

    Sequence applyMask;

    // Calculate the predicate for the mask
    Tensor more1sPredicate = popops::gteq(graph, num1sInColumnTensor, num0sInColumnTensor, applyMask, "Predicate").reshape({});

    Sequence oneBody;
    Sequence zeroBody;

    // Mask if more 1's
    Tensor maskInvert = invert(graph, columnTensor.broadcast(numCols, 1), oneBody);
    popops::addInPlace(graph, num0Counter, num1sInColumnTensor, oneBody);
    oneBody.add(Copy(maskInvert, mask));

    // Mask if more 0's
    popops::addInPlace(graph, num0Counter, num0sInColumnTensor, zeroBody);
    zeroBody.add(Copy(columnTensor.broadcast(numCols, 1), mask));

    // If statement
    applyMask.add(If(more1sPredicate, oneBody, zeroBody, "Loop"));
    
    // Need to reshape to make scalar
    Tensor moreReadingsPredicate = popops::gt(graph, numReadings, oneTensor, applyMask, "Predicate").reshape({});
    applyMask.add(Copy(moreReadingsPredicate, loopPredicate));
    
    // Increase the counter
    popops::addInPlace(graph, counter, oneTensorU, applyMask);
    
    // Apply the mask
    popops::mulInPlace(graph, inputCopyTensor, mask, applyMask);

    //
    // If we have 1 element left then we make sure to set the loop predicate to false
    //
    Sequence breakSequence;
    breakSequence.add(Copy(falseTensorC, loopPredicate));
    
    // Confitionally exit the loop if we only have 1 reading left
    loop.add(If(moreReadings, applyMask, breakSequence ,"ApplyMask"));

    //
    // For each column 
    // 
    algorithm.add(RepeatWhileTrue(Sequence(), loopPredicate.reshape({}), loop, "Repeat"));
 
    // 
    // Reduce the rows to be left with the resulting row
    //
    Tensor finalBitmap = popops::reduce(graph, inputCopyTensor, INT, {0}, {popops::Operation::ADD}, algorithm, "ColumnReduction");
    algorithm.add(PrintTensor("CO2 Scrubber Rating (Bitmap) =", finalBitmap));

    Tensor co2SrTensorParts = popops::mul(graph, finalBitmap, powersOfTwoTruncate, algorithm, "CalculateCO2SRart");
    co2SrTensor = popops::reduce(graph, co2SrTensorParts, INT, {0}, {popops::Operation::ADD}, algorithm, "CalculateCO2SR");
    algorithm.add(PrintTensor("CO2 Scrubber Rating is = ", co2SrTensor));  
  }

  // 
  // Finally multiple epsilon and gamma together.
  //
  Tensor resultTensor = popops::mul(graph, ogrTensor, co2SrTensor,  algorithm, "Multiply");
  algorithm.add(PrintTensor("Result is = ", resultTensor));  

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

