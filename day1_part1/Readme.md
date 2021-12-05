# Day 1

## Question : Sonar Sweep

You're minding your own business on a ship at sea when the overboard alarm goes off! You rush to see if you can help. Apparently, one of the Elves tripped and accidentally sent the sleigh keys flying into the ocean!

Before you know it, you're inside a submarine the Elves keep ready for situations like this. It's covered in Christmas lights (because of course it is), and it even has an experimental antenna that should be able to track the keys if you can boost its signal strength high enough; there's a little meter that indicates the antenna's signal strength by displaying 0-50 stars.

Your instincts tell you that in order to save Christmas, you'll need to get all fifty stars by December 25th.

Collect stars by solving puzzles. Two puzzles will be made available on each day in the Advent calendar; the second puzzle is unlocked when you complete the first. Each puzzle grants one star. Good luck!

As the submarine drops below the surface of the ocean, it automatically performs a sonar sweep of the nearby sea floor. On a small screen, the sonar sweep report (your puzzle input) appears: each line is a measurement of the sea floor depth as the sweep looks further and further away from the submarine.

For example, suppose you had the following report:

199
200
208
210
200
207
240
269
260
263

This report indicates that, scanning outward from the submarine, the sonar sweep found depths of 199, 200, 208, 210, and so on.

The first order of business is to figure out how quickly the depth increases, just so you know what you're dealing with - you never know if the keys will get carried into deeper water by an ocean current or a fish or something.

To do this, count the number of times a depth measurement increases from the previous measurement. (There is no measurement before the first measurement.) In the example above, the changes are as follows:

199 (N/A - no previous measurement)
200 (increased)
208 (increased)
210 (increased)
200 (decreased)
207 (increased)
240 (increased)
269 (increased)
260 (decreased)
263 (increased)

In this example, there are 7 measurements that are larger than the previous measurement.

How many measurements are larger than the previous measurement?

## Approach

For this question I am going to read in the input data (data.txt) and then map it across the tiles of the ipu as a vector. I am fact going to create two vectors with the second one offset by 1.

I am then going to subtract one vector from the other and determine which values are greater than 0. Then sum the values to work out the number of increasing measurements

Using the approach above

```
Original Data : 199 200 208 210 200 207
Offset Data   : 199 199 200 208 210 200
Subtract      : 0   1   8   2   -10 7
Greater than 0: 0   1   1   1   0   1
```

The sum the `1`'s in the final vector.

Note : When offsetting the second vector, I set the first element to the same as the original data, so the result will be 0.

## To Run

1. You will need to have activate the Poplar SDK
2. Compile using `day1$ g++ --std=c++11 day1.cpp -lpoplar -lpopops -lpoputil -o day1`
3. Run `./day1`
4. To run with profiling `POPLAR_ENGINE_OPTIONS='{"autoReport.all":"true"}' ./day1`
