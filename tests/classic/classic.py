""" The MIT License (MIT)

    Copyright (c) 2015 Kyle Hollins Wray, University of Massachusetts

    Permission is hereby granted, free of charge, to any person obtaining a copy of
    this software and associated documentation files (the "Software"), to deal in
    the Software without restriction, including without limitation the rights to
    use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
    the Software, and to permit persons to whom the Software is furnished to do so,
    subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
    FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
    COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
"""

import os
import sys

thisFilePath = os.path.dirname(os.path.realpath(__file__))

sys.path.append(os.path.join(thisFilePath, "..", "..", "python"))
from nova.pomdp import *

from pylab import *


numTrials = 1
numBeliefSteps = 4


files = [
        {'name': "tiger", 'filename': "tiger_95.pomdp", 'filetype': "pomdp", 'beliefStepSize': 8},
        {'name': "grid-4x3", 'filename': "4x3_95.pomdp", 'filetype': "pomdp", 'beliefStepSize': 16},
        {'name': "tiger-grid", 'filename': "tiger_grid.pomdp", 'filetype': "pomdp", 'beliefStepSize': 32},
        {'name': "hallway2", 'filename': "hallway2.pomdp", 'filetype': "pomdp", 'beliefStepSize': 32},
        #{'name': "tag", 'filename': "tag.pomdp", 'filetype': "pomdp", 'beliefStepSize': 64},
        #{'name': "auv-navigation", 'filename': "auvNavigation.pomdp", 'filetype': "pomdp", 'beliefStepSize': 128},
        #{'name': "rock-sample (7x8)", 'filename': "rockSample_7_8.pomdp", 'filetype': "pomdp", 'beliefStepSize': 256},
        {'name': "drive_san_francisco", 'filename': "drive_san_francisco.pomdp", 'filetype': "pomdp", 'beliefStepSize': 32},
        {'name': "drive_seattle", 'filename': "drive_seattle.pomdp", 'filetype': "pomdp", 'beliefStepSize': 64},
        {'name': "drive_new_york_city", 'filename': "drive_new_york_city.pomdp", 'filetype': "pomdp", 'beliefStepSize': 64},
        {'name': "drive_boston", 'filename': "drive_boston.pomdp", 'filetype': "pomdp", 'beliefStepSize': 128},
        ]

timings = {f['name']: {'cpu': [0.0 for j in range(numBeliefSteps)],
                       'gpu': [0.0 for j in range(numBeliefSteps)]} for f in files}

for f in files:
    print(f['name'])

    filename = os.path.join(thisFilePath, f['filename'])

    for p in ['gpu', 'cpu']:
        print(" - %s " % (p), end='')

        with open(os.path.join(thisFilePath, "results", "_".join([f['name'], p])) + ".csv", "w") as out:
            out.write("|B|,")
            for i in range(numTrials):
                out.write("%i," % (i + 1))
            out.write("\n")

            for j in range(numBeliefSteps):
                #print("Belief Step %i (max %i):" % (f['beliefStepSize'] * (j + 1), (f['beliefStepSize'] * numBeliefSteps)))
                out.write("%i," % (f['beliefStepSize'] * (j + 1)))

                for i in range(numTrials):
                    #print("Trial %i of %i" % (i + 1, numTrials))
                    print(".", end='')
                    sys.stdout.flush()

                    pomdp = POMDP()
                    pomdp.load(filename, filetype=f['filetype'])
                    pomdp.expand(method='random', numDesiredBeliefPoints=(f['beliefStepSize'] * (j + 1)))

                    Gamma, piResult, timing = pomdp.solve(process=p)

                    #print(pomdp)
                    #print(Gamma)
                    #print(piResult)

                    # Note: use the time.time() function, which measures wall-clock time.
                    timings[f['name']][p][j] = (float(j) * timings[f['name']][p][j] + timing[0]) / float(j + 1)

                    out.write("%.3f," % (timing[0]))

                out.write("\n")

        print()



# Plot the results.
#x = [f['beliefStepSize'] * (j + 1) for j in range(numBeliefSteps)]
#
#title("CPU/GPU Execution Time vs. Number of Belief Points")
#xlabel("Number of Belief Points")
#ylabel("Execution Time (seconds)")
#
#xlim([f['beliefStepSize'], numBeliefSteps * f['beliefStepSize']])
#xticks(x)
#
#hold(True)
#
#for f in files:
#    plot(x, timings[f['name']]['cpu'], label=('%s (CPU)' % f['name']))
#    plot(x, timings[f['name']]['gpu'], label=('%s (GPU)' % f['name']))
#
#legend()
#
#show()

