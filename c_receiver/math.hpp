#include <vector>
#include <numeric>
#include <algorithm>

// Calculate mean/variance over full dataset
template <typename T>
void mean_var(T * x, int len, int64_t * mu_out, uint64_t * var_out) {
    int64_t mu = 0;
    uint64_t var = 0;
    for (int idx=0; idx<len; ++idx) {
        mu += x[idx];
    }
    mu /= len;

    // We do a double loop to avoid precision issues, rather than doing the single loop formulation.
    for (int idx=0; idx<len; ++idx) {
        int64_t d = x[idx] - mu;
        var += d*d;
    }
    var /= (len - 1);

    *mu_out = mu;
    *var_out = var;
}

// Calculate mean/variance after applying an acceptance mask to elements of `x`
template <typename T>
void masked_mean_var(T * x, uint8_t * mask, int len, int64_t * mu_out, uint64_t * var_out) {
    int64_t mu_clean = 0;
    uint64_t var_clean = 0;
    int16_t num_accepted = 0;
    for (int idx=0; idx<len; ++idx) {
        if (mask[idx] == 1) {
            mu_clean += x[idx];
            num_accepted += 1;
        }
    }
    mu_clean /= num_accepted;

    // Sanity check
    if (num_accepted < 2) {
        printf("WE REJECTED TOO MANY?!\n");
        *mu_out = 0;
        *var_out = 0;
        return;
    }

    for (int idx=0; idx<len; ++idx) {
        int64_t d = x[idx] - mu_clean;
        if (mask[idx] == 1) {
            var_clean += d*d;
        }
    }
    var_clean /= (num_accepted - 1);

    // Finally, store into mu_ptr and var_ptr:
    *mu_out = mu_clean;
    *var_out = var_clean;
}

// Build a mask to exclude datapoints that contribute more than their fair share of variance
template <typename T>
int build_var_mask(T * x, int len, uint8_t * mask, float var_thresh = 10.0f) {
    // If we don't have enough data, then just set the acceptance mask to all 1's
    if (len < 10) {
        memset(mask, 1, sizeof(uint8_t)*len);
        return len;
    }

    // Calculate sample mu and var
    int64_t mu = 0;
    uint64_t var = 0;
    mean_var(x, len, &mu, &var);

    // Next, use these initial values to reject outliers; we define outliers as
    // anything that contributes more than 10x its expected variance ("expected"
    // variance is sample variance / number of samples)
    uint64_t thresh = (uint64_t)(var_thresh*var/len);
    int num_accepted = 0;
    for (int idx=0; idx<len; ++idx) {
        int64_t d = x[idx] - mu;
        if ((uint64_t)(d*d) <= thresh) {
            //printf("[%03d] d^2: 0x%llx (<= 0x%llx)\n", idx, d*d, thresh);
            mask[idx] = 1;
            num_accepted += 1;
        } else {
            //printf("[%03d] d^2: 0x%llx (>  0x%llx)\n", idx, d*d, thresh);
            mask[idx] = 0;
        }
    }
    return num_accepted;
}

template <typename T, typename U>
void masked_linreg(const U * xs, const T * ys, uint8_t * mask, int len, double * m, double * b, double * unexp_var) {
    double sumx = 0.0;
    double sumx2 = 0.0;
    double sumxy = 0.0;
    double sumy = 0.0;
    double sumy2 = 0.0;

    // Collect initial statistics on x and y
    for (int idx=0; idx<len; idx++) {
        if (mask[idx] == 1) {
            // Because the timestamps are shifted up crazy high, we always subtract out the earliest timestamp,
            // then divide to get the numbers into a manageable unit
            double x_idx = (xs[idx] - xs[len - 1])/1000000000.0;
            //printf("[%d] x: %.1fs, y: %lld\n", idx, x_idx, ys[idx]);
            sumx  += x_idx;
            sumx2 += x_idx * x_idx;
            sumxy += x_idx * ys[idx];
            sumy  += ys[idx];
            sumy2 += ys[idx] * ys[idx];
        }
    }

    double denom = (len * sumx2 - sumx*sumx);
    double _m = (len * sumxy  -  sumx * sumy) / denom;
    double _b = (sumy * sumx2  -  sumx * sumxy) / denom;
    double var = 0.0;
    for (int idx=0; idx<len; idx++) {
        if (mask[idx] == 1) {
            double x_idx = (xs[idx] - xs[len - 1])/1000000000.0;
            double y_hat = x_idx * _m + _b;
            var += (ys[idx] - y_hat)*(ys[idx] - y_hat);
        }
    }
    *m = _m;
    *b = _b;
    *unexp_var = var/len;
}


template <typename T>
std::vector<size_t> sort_indexes(const std::vector<T> &v) {
    // initialize original index locations
    std::vector<size_t> idx(v.size());
    std::iota(idx.begin(), idx.end(), 0);

    // sort indexes based on comparing values in v
    // using std::stable_sort instead of std::sort
    // to avoid unnecessary index re-orderings
    // when v contains elements of equal values
    std::stable_sort(
        idx.begin(),
        idx.end(),
        [&v](size_t i1, size_t i2) {return v[i1] < v[i2];}
    );

    return idx;
}

template <typename T>
void build_min_mask(T * x, int len, uint8_t * mask, float proportion = 0.2f) {
    // We'll keep the bottom `proportion` percentile.  To do so, we need to sort `x`:
    std::vector<T> xvec(x, x + len);
    std::vector<size_t> idxs = sort_indexes(xvec);

    memset(mask, 0, sizeof(uint8_t)*len);
    for (int idx=0; idx<((int)(proportion*len)); idx++) {
        mask[idxs[idx]] = 1;
    }
}

template <typename U, typename T>
void diff(U * x, int len, T * y) {
    for (int idx=0; idx<(len-1); ++idx) {
        y[idx] = x[idx] - x[idx+1];
    }
}

template <typename U, typename T>
void subtract(U * a, U * b, int len, T * y) {
    for (int idx=0; idx<len; ++idx) {
        y[idx] = (T)(a[idx] - b[idx]);
    }
}


// Calculate the root mean square of a buffer
template <typename T>
T rms(T * buffer) {
    T accum = T(0);
    for (int i=0; i<BUFFSIZE; ++i) {
        accum += buffer[i]*buffer[i];
    }
    return sqrt(accum/BUFFSIZE);
}