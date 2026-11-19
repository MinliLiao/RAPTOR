#ifndef _RAPTOR_MCALITEINTERFACE_H_
#define _RAPTOR_MCALITEINTERFACE_H_

#include <cmath>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
#include <mca.h>
namespace {
  enum mcalite_mode { // Comment copied from mca.h
    RR,  /* Random Rounding (RR): round(inexact(x op y)). */
    PB,  /* Precision Bounding (PB): round(inexact(x) op inexact(y)). */
    FULL /* Full MCA: round(inexact(inexact(x) op inexact(y))). */ 
  };
  std::atomic<uint64_t> rng_stream;
  struct mcalite_accumulate_stats {
    double count = 0;
    double mu = 0;
    double variance = 0;
    void accumulate_stats(double in_mu, double in_sigma) {
      mu += in_mu;
      variance += (in_sigma * in_sigma);
      count += 1;
    }
    std::string str() {
      std::stringstream ss;
      ss << "sum(mu) = " << mu << ", sum(variance) = " << variance;
      ss << ", call count = " << count;
      return ss.str();
    }
  };
}
struct __raptor_mcalite_context_t {
  uint64_t rng_seed = 42;
  uint64_t rng_stream_id = 0;
  int t = 24; // virtual precision
  int env_t = 24; // virtual precision from the environment variable
  size_t ntrials = 5; // Number of repetition per operation
  mcalite_mode mode = mcalite_mode::PB;
  bool quiet = false;
  bool mean_as_result = false;
  std::vector<double> results;
  mca_stats stats;
  std::map<const char *, mcalite_accumulate_stats> acc_stats;
  mca_rng rng;
  void print_stats(mca_stats * in) {
    std::cout << "mu = " << in->mu << ", sigma = " << in->sigma;
    std::cout << ", s = " << in->s << ", t = " << t << std::endl;
  }
  int calc_stats(const char *loc, const char * op) {
    int err = mca_compute_stats(results.data(), ntrials, &stats);
    if (err == 0) {
      if (!quiet) { 
        std::cout << "MCAlite stats of stream " << rng_stream_id;
        std::cout << " at " << loc << ": op = " << op << ", ";
        print_stats(&stats); 
      }
      acc_stats[loc].accumulate_stats(stats.mu, stats.sigma);
    }
    return err;
  }
  void parse_env() {
    // Get the seed, t, ntrials and mode from env var.
    char *env_val = getenv("MCA_SEED");
    if (env_val != nullptr) {
      std::string env_str(env_val);
      rng_seed = std::stoul(env_str);
    }
    env_val = getenv("MCA_T");
    if (env_val != nullptr) {
      std::string env_str(env_val);
      t = std::stoi(env_str);
      env_t = t;
    }
    env_val = getenv("MCA_NTRIALS");
    if (env_val != nullptr) {
      std::string env_str(env_val);
      ntrials = std::stoul(env_str);
    }
    env_val = getenv("MCA_MODE");
    if (env_val != nullptr) {
      std::string env_str(env_val);
      if (env_str == "RR") { mode = mcalite_mode::RR; }
      else if (env_str == "PB") { mode = mcalite_mode::PB; }
      else if (env_str == "FULL") { mode = mcalite_mode::FULL; }
      else {
        std::cerr << "Error: invalid MCA_MODE value " << env_str;
        std::cerr << ", valid MCA_MODE options are RR, PB and FULL.";
        std::cerr << std::endl;
        abort();
      }
    }
    env_val = getenv("MCA_QUIET");
    if (env_val != nullptr) {
      quiet = true;
    }
    env_val = getenv("MCA_USE_MEAN");
    if (env_val != nullptr) {
      mean_as_result = true;
    }
  }
  std::string mode_str() {
    std::string str = "FULL";
    switch(mode) {
      case mcalite_mode::RR: str = "RR"; break;
      case mcalite_mode::PB: str = "PB"; break;
      default: break; // mcalite_mode::FULL
    }
    return str;
  }
  void print() {
    std::cout << "rng_seed = " << rng_seed;
    std::cout << ", rng_stream = " << rng_stream_id << ", t = " << t;
    std::cout << ", ntrials = " << ntrials << ", mode = " << mode_str();
  }
  __raptor_mcalite_context_t() {
    parse_env();
    results.resize(ntrials);
    rng_stream_id = rng_stream++;
    mca_rng_seed(&rng, rng_seed, rng_stream_id);
    if (!quiet) {
      print(); std::cout << std::endl;
    }
  }
  ~__raptor_mcalite_context_t() {
    for (auto acc_stat : acc_stats) {
      std::cout << "MCAlite accumulated stats of stream " << rng_stream_id;
      std::cout << " at " << acc_stat.first;
      std::cout << ": " << acc_stat.second.str() << std::endl;
    }
  }
};
extern thread_local __raptor_mcalite_context_t __raptor_mcalite_context;

#define __RAPTOR_MCALITE_CALL_MODE_EXIST_SFINAE(prefix, OP, MODE, FROM_TY)    \
  template <typename, typename, typename = std::void_t<>>                     \
  struct __raptor_mcalite_##prefix##_##MODE##_##FROM_TY##_exists :            \
    std::false_type {};                                                       \
  template <typename T, typename ...Args>                                     \
  struct __raptor_mcalite_##prefix##_##MODE##_##FROM_TY##_exists<T,           \
    std::tuple<Args...>, std::void_t<decltype(mca_##OP##_##MODE(              \
      std::declval<T>(), std::declval<Args>()..., std::declval<int>(),        \
      std::declval<size_t>(), std::declval<mca_rng *>(),                      \
      std::declval<double *>()))>> : std::true_type {};                       \
  template <typename T, typename ...Args>                                     \
  inline constexpr                                                            \
  bool __raptor_mcalite_##prefix##_##MODE##_##FROM_TY##_exists_v =            \
    __raptor_mcalite_##prefix##_##MODE##_##FROM_TY##_exists<T,                \
      std::tuple<Args...>>::value;                                            \
  template<typename... Args>                                                  \
  int __raptor_mcalite_##prefix##_##MODE##_##FROM_TY##_call(Args... args) {   \
    if constexpr(                                                             \
      __raptor_mcalite_##prefix##_##MODE##_##FROM_TY##_exists_v<Args...>      \
    ) {                                                                       \
      return mca_##OP##_##MODE(std::forward<Args>(args)...,                   \
        __raptor_mcalite_context.t, __raptor_mcalite_context.ntrials,         \
        &(__raptor_mcalite_context.rng),                                      \
        __raptor_mcalite_context.results.data());                             \
    } else {                                                                  \
      fprintf(stderr, "ERROR: mca_%s_%s doesn't exist\n", #OP, #MODE);        \
      abort(); return -1;                                                     \
    }                                                                         \
  }
#define __RAPTOR_MCALITE_CALL_EXIST_SFINAE(prefix, OP, FROM_TY)               \
  __RAPTOR_MCALITE_CALL_MODE_EXIST_SFINAE(prefix, OP, rr, FROM_TY)            \
  __RAPTOR_MCALITE_CALL_MODE_EXIST_SFINAE(prefix, OP, pb, FROM_TY)            \
  __RAPTOR_MCALITE_CALL_MODE_EXIST_SFINAE(prefix, OP, full, FROM_TY)
#define __RAPTOR_MCALITE_OP_FUNC_BODY(prefix, OP, FROM_TY, ...)               \
  {                                                                           \
    int err = 0;                                                              \
    switch (__raptor_mcalite_context.mode) {                                  \
      case mcalite_mode::RR:                                                  \
        err = __raptor_mcalite_##prefix##_rr_##FROM_TY##_call(__VA_ARGS__);   \
        break;                                                                \
      case mcalite_mode::PB:                                                  \
        err = __raptor_mcalite_##prefix##_pb_##FROM_TY##_call(__VA_ARGS__);   \
        break;                                                                \
      case mcalite_mode::FULL:                                                \
        err = __raptor_mcalite_##prefix##_full_##FROM_TY##_call(__VA_ARGS__); \
        break;                                                                \
      default: err = -1; break;                                               \
    }                                                                         \
    if (err == 0 && __raptor_mcalite_context.ntrials > 1) {                   \
      err = __raptor_mcalite_context.calc_stats(loc, #OP);                    \
    }                                                                         \
    if (err != 0) {                                                           \
      std::cerr << "Error in mode " << __raptor_mcalite_context.mode;         \
      std::cerr << " with op " << #OP << " at " << loc << std::endl;          \
      abort();                                                                \
    }                                                                         \
    if (__raptor_mcalite_context.mean_as_result &&                            \
        __raptor_mcalite_context.ntrials > 1                                  \
    ) { return __raptor_mcalite_context.stats.mu; }                           \
    else { return __raptor_mcalite_context.results[0]; }                      \
  }

#endif // _RAPTOR_MCALITEINTERFACE_H_