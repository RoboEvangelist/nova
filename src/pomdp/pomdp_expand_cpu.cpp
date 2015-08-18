/**
 *  The MIT License (MIT)
 *
 *  Copyright (c) 2015 Kyle Hollins Wray, University of Massachusetts
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 *  the Software, and to permit persons to whom the Software is furnished to do so,
 *  subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 *  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 *  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#include "pomdp_expand_cpu.h"
#include "error_codes.h"

#include <stdio.h>
#include <cstring>
#include <cstdlib>
#include <time.h>
#include <cmath>


// This is determined by hardware, so what is below is a 'safe' guess. If this is off, the
// program might return 'nan' or 'inf'. These come from IEEE floating-point standards.
#define FLT_MAX 1e+35
#define FLT_MIN -1e+35
#define FLT_ERR_TOL 1e-9


int pomdp_expand_construct_belief_cpu(POMDP *pomdp, unsigned int i, float *b)
{
    for (unsigned int s = 0; s < pomdp->n; s++) {
        b[s] = 0.0f;
    }
    for (unsigned int j = 0; j < pomdp->rz; j++) {
        unsigned int s = pomdp->Z[i * pomdp->rz + j];
        if (s < 0) {
            break;
        }
        b[s] = pomdp->B[i * pomdp->rz + j];
    }

    return NOVA_SUCCESS;
}


int pomdp_expand_belief_update_cpu(POMDP *pomdp, const float *b, unsigned int a, unsigned int o, float *bp)
{
    for (unsigned int sp = 0; sp < pomdp->n; sp++) {
        bp[sp] = 0.0f;
    }

    for (unsigned int s = 0; s < pomdp->n; s++) {
        for (unsigned int i = 0; i < pomdp->ns; i++) {
            int sp = pomdp->S[s * pomdp->m * pomdp->ns + a * pomdp->ns + i];
            if (sp < 0) {
                break;
            }

            bp[sp] += pomdp->T[s * pomdp->m * pomdp->ns + a * pomdp->ns + i] * b[s];
        }
    }

    float normalizingConstant = 0.0f;

    for (unsigned int sp = 0; sp < pomdp->n; sp++) {
        bp[sp] *= pomdp->O[a * pomdp->n * pomdp->z + sp * pomdp->z + o];

        normalizingConstant += bp[sp];
    }

    for (unsigned int sp = 0; sp < pomdp->n; sp++) {
        bp[sp] /= normalizingConstant;
    }

    return NOVA_SUCCESS;
}


int pomdp_expand_probability_observation_cpu(POMDP *pomdp, const float *b, unsigned int a, unsigned int o, float &prObs)
{
    prObs = 0.0f;

    for (unsigned int s = 0; s < pomdp->n; s++) {
        float val = 0.0f;

        for (unsigned int i = 0; i < pomdp->ns; i++) {
            int sp = pomdp->S[s * pomdp->m * pomdp->ns + a * pomdp->ns + i];
            if (sp < 0) {
                break;
            }

            val += pomdp->T[s * pomdp->m * pomdp->ns + a * pomdp->ns + i] *
                    pomdp->O[a * pomdp->n * pomdp->z + sp * pomdp->z + o];
        }

        prObs += val * b[s];
    }

    return NOVA_SUCCESS;
}


int pomdp_expand_update_max_non_zero_values(POMDP *pomdp, const float *b, unsigned int *maxNonZeroValues)
{
    unsigned int numNonZeroValues = 0;
    for (unsigned int s = 0; s < pomdp->n; s++) {
        if (b[s] > 0.0f) {
            numNonZeroValues++;
        }
    }
    if (numNonZeroValues > *maxNonZeroValues) {
        *maxNonZeroValues = numNonZeroValues;
    }

    return NOVA_SUCCESS;
}


int pomdp_expand_random_cpu(POMDP *pomdp, unsigned int numDesiredBeliefPoints, unsigned int *maxNonZeroValues, float *Bnew)
{
    srand(time(nullptr));

    *maxNonZeroValues = 0;

    // Setup the initial belief point.
    float *b0 = new float[pomdp->n];
    pomdp_expand_construct_belief_cpu(pomdp, 0, b0);

    float *b = new float[pomdp->n];
    unsigned int i = 1;

    // The first one is always the initial seed belief.
    memcpy(&Bnew[0 * pomdp->n], b0, pomdp->n * sizeof(float));

    // For each belief point we want to expand. Each step will generate a new trajectory
    // and add the resulting belief point to B.
    while (i < numDesiredBeliefPoints) {
        // Randomly pick a horizon for this trajectory. We do this because some domains transition
        // beliefs away from areas on the (n-1)-simplex, never to return. This ensures many paths
        // are added.
        unsigned int h = (unsigned int)((float)rand() / (float)RAND_MAX * (float)(pomdp->horizon + 1));

        // Setup the belief used in exploration.
        memcpy(b, b0, pomdp->n * sizeof(float));

        // Follow a random trajectory with length equal to this horizon.
        for (unsigned int t = 0; t < h; t++) {
            // Randomly pick an action.
            unsigned int a = (unsigned int)((float)rand() / (float)RAND_MAX * (float)pomdp->m);

            float currentNumber = 0.0f;
            float targetNumber = (float)rand() / (float)RAND_MAX;

            unsigned int o = 0;
            for (unsigned int op = 0; op < pomdp->z; op++) {
                float prObs = 0.0f;
                pomdp_expand_probability_observation_cpu(pomdp, b, a, op, prObs);
                currentNumber += prObs;

                if (currentNumber >= targetNumber) {
                    o = op;
                    break;
                }
            }

            // Follow the belief update equation to compute b' for all state primes s'.
            float *bp = new float[pomdp->n];
            pomdp_expand_belief_update_cpu(pomdp, b, a, o, bp);
            memcpy(b, bp, pomdp->n * sizeof(float));
            delete [] bp;

            // Determine how many non-zero values exist and update rz.
            pomdp_expand_update_max_non_zero_values(pomdp, b, maxNonZeroValues);

            // Assign the computed belief for this trajectory.
            memcpy(&Bnew[i * pomdp->n], b, pomdp->n * sizeof(float));

            // Stop if we have met the belief point quota.
            i++;
            if (i >= numDesiredBeliefPoints) {
                break;
            }
        }
    }

    delete [] b;
    delete [] b0;

    return NOVA_SUCCESS;
}


int pomdp_expand_distinct_beliefs_cpu(POMDP *pomdp, unsigned int *maxNonZeroValues, float *Bnew)
{
    *maxNonZeroValues = 0;

    for (unsigned int i = 0; i < pomdp->r; i++) {
        // Construct a belief to use in computing b'.
        float *b = new float[pomdp->n];
        pomdp_expand_construct_belief_cpu(pomdp, i, b);

        float *bpStar = new float[pomdp->n];
        float bpStarValue = FLT_MIN;

        // For this belief point, find the action-observation pair that is most distinct
        // from the current set of beliefs B. This will be added to Bnew.
        for (unsigned int a = 0; a < pomdp->m; a++) {
            for (unsigned int o = 0; o < pomdp->z; o++) {
                // Compute b'.
                float *bp = new float[pomdp->n];
                pomdp_expand_belief_update_cpu(pomdp, b, a, o, bp);

                // Compute min|b - b'| for all beliefs b in B.
                float jStarValue = FLT_MAX;

                for (unsigned int j = 0; j < pomdp->r; j++) {
                    float *btmp = new float[pomdp->n];
                    pomdp_expand_construct_belief_cpu(pomdp, j, btmp);

                    float value = 0.0f;
                    for (unsigned int s = 0; s < pomdp->n; s++) {
                        value += std::fabs(btmp[s] - bp[s]);
                    }

                    if (value < jStarValue) {
                        jStarValue = value;
                    }

                    delete [] btmp;
                }

                // Now, determine if this was the largest b' we found so far. If so, remember it.
                if (jStarValue > bpStarValue) {
                    memcpy(bpStar, bp, pomdp->n * sizeof(float));
                    bpStarValue = jStarValue;
                }

                delete [] bp;
            }
        }

        // Determine how many non-zero values exist and update rz.
        pomdp_expand_update_max_non_zero_values(pomdp, bpStar, maxNonZeroValues);

        // For this belief b, add a new belief bp* = max_{a, o} min_{b'' in B} |b'(b, a, o) - b''|_1.
        memcpy(&Bnew[i * pomdp->n], bpStar, pomdp->n * sizeof(float));

        delete [] b;
        delete [] bpStar;
    }

    return NOVA_SUCCESS;
}


int pomdp_expand_pema_cpu(POMDP *pomdp, float Rmin, float Rmax, float *Gamma, unsigned int *maxNonZeroValues, float *Bnew)
{
    *maxNonZeroValues = 0;

    for (unsigned int i = 0; i < pomdp->r; i++) {
        unsigned int aStar = 0;
        float aStarValue = FLT_MIN;

        // Construct a belief to use in computing b'.
        float *b = new float[pomdp->n];
        pomdp_expand_construct_belief_cpu(pomdp, i, b);

        // During computing the max action, we will store the observation which introduced
        // the maximal error, i.e., one with highest value of epsilon(b'(b, a, o)).
        unsigned int *oStar = new unsigned int[pomdp->m];
        float *oStarEpsilonBeliefPrime = new float[pomdp->m];
        for (unsigned int a = 0; a < pomdp->m; a++) {
            oStar[a] = 0;
            oStarEpsilonBeliefPrime[a] = FLT_MIN;
        }

        // At this belief b, select the action which maximizes the sum over observations
        // of the probability of this observation times the expected worst-case error
        // at the *next* belief b' following a and o.
        for (unsigned int a = 0; a < pomdp->m; a++) {
            float aValue = 0.0f;

            for (unsigned int o = 0; o < pomdp->z; o++) {
                // Compute Pr(o|b,a).
                float probObsGivenBeliefAction = 0.0f;

                for (unsigned int j = 0; j < pomdp->rz; j++) {
                    int s = pomdp->Z[i * pomdp->rz + j];
                    if (s < 0) {
                        break;
                    }

                    for (unsigned int sp = 0; sp < pomdp->n; sp++) {
                        probObsGivenBeliefAction += pomdp->O[a * pomdp->n * pomdp->z + sp * pomdp->z + o] * pomdp->B[i * pomdp->rz + j];
                    }
                }

                // With the current action and observation compute b'(b, a, o).
                float *bp = new float[pomdp->n];
                pomdp_expand_belief_update_cpu(pomdp, b, a, o, bp);

                // Compute epsilon(b'(b, a, o)).
                float epsilonBeliefPrime = 0.0f;
                for (unsigned int s = 0; s < pomdp->n; s++) {
                    if (bp[s] >= b[s]) {
                        epsilonBeliefPrime += (Rmax / (1.0f - pomdp->gamma) - Gamma[i * pomdp->n + s]) * (bp[s] - b[s]);
                    } else {
                        epsilonBeliefPrime += (Rmin / (1.0f - pomdp->gamma) - Gamma[i * pomdp->n + s]) * (bp[s] - b[s]);
                    }
                }

                // For this action, update the maximal oStar[a].
                if (epsilonBeliefPrime > oStarEpsilonBeliefPrime[a]) {
                    oStarEpsilonBeliefPrime[a] = epsilonBeliefPrime;
                    oStar[a] = o;
                }

                // Add the result to find the maximal action.
                aValue += probObsGivenBeliefAction * epsilonBeliefPrime;

                delete [] bp;
            }

            if (aValue > aStarValue) {
                aStar = a;
                aStarValue = aValue;
            }
        }

        // With the best action, compute b'(b, aStar, oStar[aStar]).
        float *bp = new float[pomdp->n];
        pomdp_expand_belief_update_cpu(pomdp, b, aStar, oStar[aStar], bp);

        // Determine how many non-zero values exist and update rz.
        pomdp_expand_update_max_non_zero_values(pomdp, bp, maxNonZeroValues);

        // Add this belief to Bnew.
        memcpy(&Bnew[i * pomdp->n], bp, pomdp->n * sizeof(float));

        delete [] b;
        delete [] bp;
        delete [] oStar;
        delete [] oStarEpsilonBeliefPrime;
    }

    return NOVA_SUCCESS;
}
