/*
 * Adapted from https://github.com/efficient/msls-eval/blob/master/zipf.h, original license:
 *
 * Copyright 2014, 2015, 2016 Carnegie Mellon University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>

static double fast_rand_d(uint64_t *state) {
    *state = (*state * 0x5deece66dUL + 0xbUL) & ((1UL << 48) - 1);
    return (double) *state / (double) ((1UL << 48) - 1);
}

struct zipf_gen_state {
    uint64_t n;    // number of items (input)
    double theta;  // skewness (input) in (0, 1); or, 0 = uniform, 1 = always zero
    double alpha;  // only depends on theta
    double thres;  // only depends on theta
    uint64_t last_n;  // last n used to calculate the following
    double dbl_n;
    double zetan;
    double eta;
    // unsigned short rand_state[3];		// prng state
    uint64_t rand_state;
};

static double pow_approx(double a, double b) {
    // from
    // http://martin.ankerl.com/2012/01/25/optimized-approximative-pow-in-c-and-cpp/

    // calculate approximation with fraction of the exponent
    int e = (int) b;
    union {
        double d;
        int x[2];
    } u = {a};
    u.x[1] = (int) ((b - (double) e) * (double) (u.x[1] - 1072632447) + 1072632447.);
    u.x[0] = 0;

    // exponentiation by squaring with the exponent's integer part
    // double r = u.d makes everything much slower, not sure why
    // TODO: use popcount?
    double r = 1.;
    while (e) {
        if (e & 1) r *= a;
        a *= a;
        e >>= 1;
    }

    return r * u.d;
}

static void zipf_init(struct zipf_gen_state *state, uint64_t n, double theta,
                      uint64_t rand_seed) {
    assert(n > 0);
    if (theta > 0.992 && theta < 1)
        fprintf(stderr, "theta > 0.992 will be inaccurate due to approximation\n");
    if (theta >= 1. && theta < 40.) {
        fprintf(stderr, "theta in [1., 40.) is not supported\n");
        assert(false);
    }
    assert(theta == -1. || (theta >= 0. && theta < 1.) || theta >= 40.);
    assert(rand_seed < (1UL << 48));
    memset(state, 0, sizeof(struct zipf_gen_state));
    state->n = n;
    state->theta = theta;
    if (theta == -1.)
        rand_seed = rand_seed % n;
    else if (theta > 0. && theta < 1.) {
        state->alpha = 1. / (1. - theta);
        state->thres = 1. + pow_approx(0.5, theta);
    } else {
        state->alpha = 0.;  // unused
        state->thres = 0.;  // unused
    }
    state->last_n = 0;
    state->zetan = 0.;
    // state->rand_state[0] = (unsigned short)(rand_seed >> 0);
    // state->rand_state[1] = (unsigned short)(rand_seed >> 16);
    // state->rand_state[2] = (unsigned short)(rand_seed >> 32);
    state->rand_state = rand_seed;
}

static double zeta(uint64_t last_n, double last_sum, uint64_t n, double theta) {
    if (last_n > n) {
        last_n = 0;
        last_sum = 0.;
    }
    while (last_n < n) {
        last_sum += 1. / pow_approx((double) last_n + 1., theta);
        last_n++;
    }
    return last_sum;
}

static uint64_t zipf_next(struct zipf_gen_state *state) {
    if (state->last_n != state->n) {
        if (state->theta > 0. && state->theta < 1.) {
            state->zetan = zeta(state->last_n, state->zetan, state->n, state->theta);
            state->eta = (1. - pow_approx(2. / (double) state->n, 1. - state->theta)) /
                         (1. - zeta(0, 0., 2, state->theta) / state->zetan);
        }
        state->last_n = state->n;
        state->dbl_n = (double) state->n;
    }

    if (state->theta == -1.) {
        uint64_t v = state->rand_state;
        if (++state->rand_state >= state->n) state->rand_state = 0;
        return v;
    } else if (state->theta == 0.) {
        double u = fast_rand_d(&state->rand_state);
        return (uint64_t) (state->dbl_n * u);
    } else if (state->theta >= 40.) {
        return 0UL;
    } else {
        // from J. Gray et al. Quickly generating billion-record synthetic
        // databases. In SIGMOD, 1994.

        // double u = erand48(state->rand_state);
        double u = fast_rand_d(&state->rand_state);
        double uz = u * state->zetan;
        if (uz < 1.)
            return 0UL;
        else if (uz < state->thres)
            return 1UL;
        else
            return (uint64_t) (state->dbl_n *
                               pow_approx(state->eta * (u - 1.) + 1., state->alpha));
    }
}
