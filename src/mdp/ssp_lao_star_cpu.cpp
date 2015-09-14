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


#include "mdp_vi_cpu.h"
#include "error_codes.h"

#include <stdio.h>
#include <cstring>


// This is determined by hardware, so what is below is a 'safe' guess. If this is
// off, the program might return 'nan' or 'inf'.
#define FLT_MAX 1e+35


void ssp_lao_star_bellman_update_state_cpu(unsigned int n, unsigned int ns, unsigned int m, float gamma, 
                            const int *S, const float *T, const float *R, const float *V,
                            unsigned int ne, const int *expanded,
                            unsigned int s,
                            float *VPrime, unsigned int *pi)
{
    VPrime[s] = FLT_MAX;

    // Compute min_{a in A} Q(s, a). Recall, we are dealing with rewards R as positive costs.
    for (int a = 0; a < m; a++) {
        // Compute Q(s, a) for this action.
        float Qsa = R[s * m + a];

        for (int i = 0; i < ns; i++) {
            int sp = S[s * m * ns + a * ns + i];
            if (sp < 0) {
                break;
            }

            // Note: V is marked with a negative based on visitation. If it had not been
            // visited, then it means it is using the heuristic value.
            Qsa += gamma * T[s * m * ns + a * ns + i] * std::fabs(V[sp]);
        }

        if (a == 0 || Qsa < VPrime[s]) {
            VPrime[s] = Qsa;
            pi[s] = a;
        }
    }
}


void ssp_lao_star_reset_visited(unsigned int ne, int *expanded, float *V, float *VPrime)
{
    for (unsigned int i = 0; i < ne; i++) {
        unsigned int s = expanded[i];

        if (V[s] > 0.0f) {
            V[s] = -V[s];
        }

        if (VPrime[s] > 0.0f) {
            VPrime[s] = -VPrime[s];
        }
    }
}


void ssp_lao_star_bellman_residual_cpu(unsigned int ne, int *expanded, float epsilon, float *V, float *VPrime,
                                        bool *converged)
{
    float residual = 0.0f;

    for (unsigned int i = 0; i < ne; i++) {
        int s = expanded[i];
        if (s < 0) {
            break;
        }

        float sResidual = std::fabs(std::fabs(V[s]) - std::fabs(VPrime[s]));
        residual = std::max(residual, sResidual);
    }

    if (residual < epsilon) {
        *converged = true;
    } else {
        *converged = false;
    }
}


int ssp_lao_star_expand_cpu(MDP *mdp, unsigned int *numNewlyExpandedStates)
{
    // First, reset the visited states.
    ssp_lao_star_reset_visited(mdp->ne, mdp->expanded, mdp->V, mdp->VPrime);

    // Then, reset the expanded variable.
    //for (unsigned int i = 0; i < mdp->ne; i++) {
    //    mdp->expanded[i] = -1;
    //}
    mdp->ne = 0;

    // Create a fringe state list (stack) variable, with just state s0.
    unsigned int nf = 1;
    unsigned int fringe = new unsigned int[mdp->n];
    fringe[0] = mdp->s0;

    *numNewlyExpandedStates = 0;

    // Iterate until there are no more elements on the fringe.
    while (nf != 0) {
        // Pop the last element off the stack.
        unsigned int s = fringe[nf - 1];
        nf--;

        // Check if this state has been visited. If so, continue.
        if (mdp->V[s] > 0.0f || mdp->VPrime[s] > 0.0f) {
            continue;
        }

        // Check if this state is a goal. If so, continue.
        bool isGoal = false;

        for (unsigned int i = 0; i < mdp->ng; i++) {
            if (s == mdp->goals[i]) {
                isGoal = true;
            }
        }

        if (isGoal) {
            continue;
        }

        // Otherwise, this is a newly expanded state. Perform a Bellman update and mark it.
        ssp_lao_star_bellman_update_state_cpu(mdp->n, mdp->ns, mdp->m, mdp->gamma,
                                            mdp->S, mdp->T, mdp->R, mdp->V,
                                            mdp->ne, mdp->expanded,
                                            s,
                                            mdp->VPrime, mdp->pi);

        // Add all of its children to the fringe and the overall set of expanded states.
        for (unsigned int i = 0; i < mdp->ns; i++) {
            unsigned int sp = mdp->S[s * m * ns + a * ns + i];
            if (sp < 0) {
                break;
            }

            fringe[nf] = sp;
            nf++;

            mdp->expanded[mdp->ne] = sp;
            mdp->ne++;

            *numNewlyExpandedStates++;
        }
    }

    // At the end, in post order traversal, perform Bellman updates. Note: This performs an additional
    // Bellman update for all the newly expanded states due to the lack of recursion. Since the
    // next step still has to apply value iteration until convergence anyway, and new states are likely
    // to have horrible values to start, this is fine.
    for (unsigned int i = mdp->ne - 1; i >= 0; i--) {
        unsigned int s = mdp->expanded[i];

        ssp_lao_star_bellman_update_state_cpu(mdp->n, mdp->ns, mdp->m, mdp->gamma,
                                            mdp->S, mdp->T, mdp->R, mdp->V,
                                            mdp->ne, mdp->expanded,
                                            s,
                                            mdp->VPrime, mdp->pi);
    }

    delete [] fringe;
}


int ssp_lao_star_check_convergence_cpu(MDP *mdp, bool *converged, bool *nonExpandedTipStateFound)
{
    // Compute the Bellman update in post order traversal.
    for (unsigned int i = mdp->ne - 1; i >= 0; i--) {
        unsigned int s = mdp->expanded[i];
        unsigned int a = mdp->pi[s];

        // We oscillate between V and VPrime depending on the step.
        if (mdp->currentHorizon % 2 == 0) {
            ssp_lao_star_bellman_update_cpu(mdp->n, mdp->ns, mdp->m, mdp->gamma,
                                            mdp->S, mdp->T, mdp->R, mdp->V,
                                            mdp->ne, mdp->expanded,
                                            s,
                                            mdp->VPrime, mdp->pi);
        } else {
            ssp_lao_star_bellman_update_state_cpu(mdp->n, mdp->ns, mdp->m, mdp->gamma,
                                            mdp->S, mdp->T, mdp->R, mdp->VPrime,
                                            mdp->ne, mdp->expanded,
                                            s,
                                            mdp->V, mdp->pi);
        }

        // If the action changed, then we must check if it has a successor that is not expanded yet.
        // If this is the case, then we return with a non-expanded tip state found.
        if (a != pi[s]) {
            for (unsigned int j = 0; j < mdp->ns; j++) {
                unsigned int sp = mdp->S[s * a * ns + pi[s] * ns + j];
                if (sp < 0) {
                    break;
                }

                if (mdp->V[sp] < 0.0f && mdp->VPrime[sp] < 0.0f) {
                    *nonExpandedTipStateFound = true;
                    return;
                }
            }
        }
    }

    // Compute the Bellman residual and determine if it converged or not.
    ssp_lao_star_bellman_residual_cpu(mdp->n, mdp->ne, mdp->expanded, mdp->epsilon, mdp->V, mdp->VPrime, converged);

    // Determine if any actions would lead to successors that are not in the set of expanded states (not visited).
    //ssp_lao_star_check_all_tip_states_visited(mdp->ns, mdp->S, mdp->ne, mdp->expanded, mdp->V, mdp->VPrime, nonExpandedTipStateFound);

    mdp->currentHorizon++;

    return NOVA_SUCCESS;
}


int ssp_lao_star_complete_cpu(MDP *mdp, float *V, unsigned int *pi)
{
    // Note: This 'wrapper' function is provided in order to maintain 
    // the same structure as the GPU version. In the GPU version,
    // 'complete' performs the initilization and uninitialization of
    // the MDP object on the device as well. Here, we do not need that.
    return ssp_lao_star_execute_cpu(mdp, V, pi);
}


int ssp_lap_star_initialize_cpu(MDP *mdp, float *V)
{
    // Reset the current horizon.
    mdp->currentHorizon = 0;

    // Create the variables.
    mdp->V = new float[mdp->n];
    mdp->VPrime = new float[mdp->n];
    mdp->pi = new unsigned int[mdp->n];

    mdp->ne = 0
    mdp->expanded = new int[mdp->n];

    // Copy the data from the V provided, and set default values for pi.
    // Note that these values of V are the heuristics for each state.
    // Also, the default values for the expanded states are -1, meaning
    // no expanded state is defined for the index.
    memcpy(mdp->V, V, mdp->n * sizeof(float));
    memcpy(mdp->VPrime, V, mdp->n * sizeof(float));
    for (unsigned int i = 0; i < mdp->n; i++) {
        mdp->pi[i] = 0;
        mdp->expanded[i] = -1;
    }

    return NOVA_SUCCESS;
}


int ssp_lao_star_execute_cpu(MDP *mdp, float *V, unsigned int *pi)
{
    int result;

    // First, ensure data is valid.
    if (mdp->n == 0 || mdp->ns == 0 || mdp->m == 0 ||
            mdp->S == nullptr || mdp->T == nullptr || mdp->R == nullptr ||
            mdp->gamma < 0.0f || mdp->gamma >= 1.0f || mdp->horizon < 1 ||
            mdp->ne != 0 || mdp->expanded == nullptr ||
            V == nullptr || pi == nullptr) {
        fprintf(stderr, "Error[ssp_lao_star_execute_cpu]: %s\n", "Invalid arguments.");
        return NOVA_ERROR_INVALID_DATA;
    }

    result = ssp_lao_star_initialize_cpu(mdp);
    if (result != NOVA_SUCCESS) {
        fprintf(stderr, "Error[ssp_lao_star_execute_cpu]: %s\n", "Failed to initialize the CPU variables.");
        return result;
    }

    // We continue the process of expanding and testing convergence until convergence occurs.
    bool running = true;
    while (running) {
        // Expand Step: Perform DFS (greedy actions) and construct a tree from possible stochastic transitions.
        // This continues until you have: (1) expanded all states, and (2) have reached one of the goal states.
        unsigned int numNewlyExpandedStates = 1;
        while (numNewlyExpandedStates != 0) {
            // Perform DFS (greedy actions), but mark states as visited along the way too, so it doesn't revisit them.
            // This performs a Bellman update in postorder traversal through the tree of expanded nodes.
            result = ssp_lao_star_expand_cpu(mdp, &numNewlyExpandedStates);
            if (result != NOVA_SUCCESS) {
                fprintf(stderr, "Error[ssp_lao_star_execute_cpu]: %s\n", "Failed to perform Bellman update on the CPU.");
                return result;
            }
        }

        // Check Convergence Step: Run value iteration on expanded states until: (1) it converges (done), or (2) it has
        // an optimal action has a possible successor that was not yet expanded (break and continue).
        bool converged = false;
        bool nonExpandedTipStateFound = false;

        mdp->currentHorizon = 0;
        while (mdp->currentHorizon < mdp->horizon && !converged) {
            result = ssp_lao_star_check_convergence_cpu(mdp, &converged, &nonExpandedTipStateFound);
            if (result != NOVA_SUCCESS) {
                fprintf(stderr, "Error[ssp_lao_star_execute_cpu]: %s\n", "Failed to perform Bellman update on the CPU.");
                return result;
            }

            // If we converged, then break. Or if during iteration we ever expanded a tip state that was not valid,
            // then just stop here so we can add it during the expand step on the next pass through the outer loop.
            if (converged || nonExpandedTipStateFound) {
                break;
            }
        }

        // If we converged (or simply ran out of time) and all expanded tip states were valid, then we are done.
        if (!nonExpandedTipStateFound) {
            running = false;
        }
    }

    result = ssp_lao_star_get_policy_cpu(mdp, V, pi);
    if (result != NOVA_SUCCESS) {
        fprintf(stderr, "Error[ssp_lao_star_execute_cpu]: %s\n", "Failed to get the policy.");
        return result;
    }

    result = ssp_lao_star_uninitialize_cpu(mdp);
    if (result != NOVA_SUCCESS) {
        fprintf(stderr, "Error[ssp_lao_star_execute_cpu]: %s\n", "Failed to uninitialize the CPU variables.");
        return result;
    }

    return NOVA_SUCCESS;
}


int ssp_lao_star_uninitialize_cpu(MDP *mdp)
{
    // Reset the current horizon and number of expanded states.
    mdp->currentHorizon = 0;
    mdp->ne = 0;

    // Free the expanded states set.
    if (mdp->expanded != nullptr) {
        delete [] mdp->expanded;
    }
    mdp->expanded = nullptr;

    // Free the memory for V, VPrime, and pi.
    if (mdp->V != nullptr) {
        delete [] mdp->V;
    }
    mdp->V = nullptr;

    if (mdp->VPrime != nullptr) {
        delete [] mdp->VPrime;
    }
    mdp->VPrime = nullptr;

    if (mdp->pi != nullptr) {
        delete [] mdp->pi;
    }
    mdp->pi = nullptr;

    return NOVA_SUCCESS;
}


int ssp_lao_star_get_policy_cpu(MDP *mdp, float *V, unsigned int *pi)
{
    // Determine which is the source for V based on the current horizon.
    float *Vsrc = nullptr;
    if (mdp->currentHorizon % 2 == 0) {
        Vsrc = mdp->V;
    } else {
        Vsrc = mdp->VPrime;
    }

    // Copy the final (or intermediate) result, both V and pi. This assumes memory has been allocated
    // for the variables provided. Importantly, only the values of the expanded states are copied.
    // The non-expanded states are left alone. Also, recall that V and pi in the SSP MDP are following
    // the order in which they were expanded.
    for (unsigned int i = 0; i < mdp->ne; i++) {
        unsigned int s = mdp->expanded[i];
        Vsrc[s] = mdp->V[i];
        pi[s] = mdp->pi[i];
    }

    return NOVA_SUCCESS;
}
