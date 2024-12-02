#include <iostream>
#include <algorithm>
#include <random>
#include <vector>
#include <map>
#include <chrono>
#include <parlay/primitives.h>
#include <parlay/parallel.h>

class random_generator {
    std::mt19937 eng{42};
public:
    template <typename T>
    std::vector<T> rand_vec(size_t size) {
        std::uniform_int_distribution<T> gen;
        std::vector<T> arr(size);
        for (int j = 0; j < arr.size(); ++j) {
            arr[j] = gen(eng);
        }
        return arr;
    }

    template <typename T=size_t>
    T rand(T low, T high) {
        return std::uniform_int_distribution<T>{low, high}(eng);
    }

    template <typename T=size_t>
    T rand(T high=std::numeric_limits<T>::max()) {
        return std::uniform_int_distribution<T>{0, high}(eng);
    }
} gen;

namespace Seq {
    template <typename It, typename T, typename Comp>
    std::pair<It, It> partition(It left, It right, T pivot, Comp comp) {
        if (left == right) return {left, right};
        --right;
        while (true) {
            while (comp(*left, pivot)) {
                ++left;
            }
            while (comp(pivot, *right)) {
                --right;
            }
            if (left >= right) return {left, ++right};
            std::iter_swap(left++, right--);
        }
    }

    template <bool parallel=false, typename It, typename Comp = std::less<>>
    void quicksort(It first, It last, Comp comp = {}) {
        if (first >= last) return;
        auto pivot = *(first + gen.rand(std::distance(first, last) - 1));
        auto [left, right] = partition(first, last, pivot, comp);
        parlay::par_do_if(parallel,
            [&]{ quicksort<parallel>(first, left, comp); },
            [&]{ quicksort<parallel>(right, last, comp); }
        );
    }
} // namespace Seq

namespace Par {
    struct split_options {
        bool parallel_filtering = false;
        bool parallel_copying = false;
        bool use_partition = false;
    };

    // Parlaylib doesn't have implementation of partition yet, so it is written here.
    // Approach is: do everything the same as filter, but on the last step also store "false"
    // values (on which `f` evaluated to false) after "true" values.
    //
    // Source code was taken from parlaylib's filter implementation and was modified
    // according to the described algorithm.
    template <typename In_Seq, typename F>
    auto partition(In_Seq &In, F&& f) {
        using outT = parlay::range_value_type_t<In_Seq>;
        size_t n = In.size();
        size_t l = parlay::internal::num_blocks(n, parlay::internal::_block_size);

        parlay::sequence<size_t> Sums(l);
        auto Fl = parlay::delayed_map(In, f);
        parlay::internal::sliced_for(n, parlay::internal::_block_size, [&](size_t i, size_t s, size_t e) {
            size_t r = 0;
            for (size_t j = s; j < e; j++) r += Fl[j];
            Sums[i] = r;
        });
        size_t m = parlay::scan_inplace(parlay::make_slice(Sums), parlay::plus<size_t>());
        parlay::sequence<outT> Out = parlay::sequence<outT>::uninitialized(n);
        parlay::internal::sliced_for(n, parlay::internal::_block_size, [&](size_t i, size_t s, size_t e) {
            size_t k = Sums[i];
            for (size_t j = s; j < e; j++) {
                if (Fl[j]) {
                    parlay::assign_uninitialized(Out[k++], In[j]);
                } else {
                    parlay::assign_uninitialized(Out[m + j - k], In[j]);
                }
            }
        });
        parlay::copy(Out, In);
        return m;
    }

    template <split_options opts, typename It, typename T, typename Comp>
    std::pair<It, It> split3(It first, It last, T pivot1, T pivot2, Comp comp) {
        auto in = parlay::make_slice(first, last);
        size_t n = in.size();
        size_t l, r;
        if constexpr (opts.use_partition) {
            l = partition(in, [&](auto val) { return comp(val, pivot1); });
            auto temp = in.cut(l, n);
            size_t m = partition(temp, [&](auto val) { return !comp(pivot2, val); });
            r = l + m;
        } else {
            parlay::sequence<T> left, mid, right;
            parlay::par_do3_if(opts.parallel_filtering,
                [&]{ left = parlay::filter(in, [&](auto val) { return comp(val, pivot1); });},
                [&]{ mid = parlay::filter(in, [&](auto val) { return !comp(val, pivot1) && !comp(pivot2, val); });},
                [&]{ right = parlay::filter(in, [&](auto val) { return comp(pivot2, val); });}
            );
            l = left.size();
            r = l + mid.size();
            parlay::par_do3_if(opts.parallel_copying,
                [&]{ parlay::copy(left, in); },
                [&]{ parlay::copy(mid, in.cut(l, r)); },
                [&]{ parlay::copy(right, in.cut(r, n)); }
            );
        }
        return {first + l, first + r};
    }

    template <split_options opts, typename It, typename T, typename Comp>
    std::pair<It, It> split(It first, It last, T pivot, Comp comp) {
        return split3<opts>(first, last, pivot, pivot, comp);
    }

    template <size_t Block, bool three=false, split_options opts=split_options{},
              typename It, typename Comp = std::less<>>
    void quicksort(It first, It last, Comp comp = {}) {
        if (first >= last) return;
        const size_t size = std::distance(first, last);
        if (size <= Block) {
            return Seq::quicksort<true>(first, last, comp);
        }
        if constexpr (three) {
            auto pivot1 = *(first + gen.rand(size - 1));
            auto pivot2 = *(first + gen.rand(size - 1));
            if (!comp(pivot1, pivot2)) std::swap(pivot1, pivot2);
            auto [left, right] = split3<opts>(first, last, pivot1, pivot2, comp);
            parlay::par_do3(
                [&]{ quicksort<Block, three, opts>(first, left, comp); },
                [&]{ quicksort<Block, three, opts>(left, right, comp); },
                [&]{ quicksort<Block, three, opts>(right, last, comp); }
            );
        } else {
            auto pivot = *(first + gen.rand(size - 1));
            auto [left, right] = split<opts>(first, last, pivot, comp);
            parlay::par_do(
                [&]{ quicksort<Block, three, opts>(first, left, comp); },
                [&]{ quicksort<Block, three, opts>(right, last, comp); }
            );
        }
    }

    template <size_t Block, split_options opts=split_options{},
              typename It, typename Comp = std::less<>>
    void quicksort2(It first, It last, Comp comp = {}) {
        if (first >= last) return;
        const size_t size = std::distance(first, last);
        if (size <= Block) {
            return Seq::quicksort<true>(first, last, comp);
        }
        auto pivot = *(first + gen.rand(size - 1));

        auto in = parlay::make_slice(first, last);
        size_t m;
        if constexpr (opts.use_partition) {
            m = partition(in, [&](auto val) { return comp(val, pivot); });
        } else {
            parlay::sequence<typename It::value_type> left, right;
            parlay::par_do_if(opts.parallel_filtering,
                [&] { left = parlay::filter(in, [&](auto val) { return comp(val, pivot); }); },
                [&] { right = parlay::filter(in, [&](auto val) { return !comp(val, pivot); }); }
            );
            size_t n = in.size();
            m = left.size();
            parlay::par_do_if(opts.parallel_copying,
                [&]{ parlay::copy(left, in);},
                [&]{ parlay::copy(right, in.cut(m, n));}
            );
        }

        parlay::par_do(
            [&]{ quicksort2<Block, opts>(first, first + m, comp); },
            [&]{ quicksort2<Block, opts>(first + m, last, comp); }
        );
    }
} // namespace Par

namespace testing {
    constexpr size_t ARRAY_SIZE = 1e7;

    template <typename T>
    struct Func {
        std::string name;
        T func;
    };

    template <typename... Funcs>
    void test_speed(Funcs&&... funcs) {
        const int n_attempts = 5;
        using arr_t = size_t;
        using duration_t = std::chrono::duration<double>;
        random_generator gen;
        std::map<std::string, duration_t> total;
        std::cout << "\033[1mBenchmark:\033[0m\n";
        for (int i = 1; i <= n_attempts; ++i) {
            std::vector<arr_t> arr = gen.rand_vec<arr_t>(ARRAY_SIZE);
            std::cout << "\033[1mTest " << i << ":\033[0m\n";
            ([&] {
                auto arr_copy = arr;
                auto t1 = std::chrono::high_resolution_clock::now();
                funcs.func(arr_copy.begin(), arr_copy.end());
                auto t2 = std::chrono::high_resolution_clock::now();
                duration_t d = t2 - t1;
                total[funcs.name] += d;
                std::cout << funcs.name << ": " << d << '\n';
            } (), ...);
        }
        std::cout << "\033[1mTotal:\033[0m\n";
        ((std::cout << funcs.name << ": " << total[funcs.name] << '\n'), ...);
    }

    template <typename F, typename T>
    bool test_array(std::vector<T> arr, F func) {
        auto arr_copy = arr;
        func(arr.begin(), arr.end());
        std::sort(arr_copy.begin(), arr_copy.end());
        return arr == arr_copy;
    }

    std::string result(bool res) {
        return res ? "\033[0;32mPassed\033[0m" : "\033[0;31mFailed\033[0m";
    }

    template <typename F>
    void test_correctness(F func) {
        std::mt19937 engine(123);
        using arr_t = size_t;
        random_generator gen;
        std::cout << "\033[1mTest correctness:\033[0m\n";
        {
            std::vector<int> arr;
            std::cout << "Empty array: " << result(test_array(arr, func)) << '\n';
        }
        {
            std::vector<int> arr{1};
            std::cout << "Single element array: " << result(test_array(arr, func)) << '\n';
        }
        {
            bool res = true;
            for (int sz = 2; sz <= 16 && res; ++sz) {
                for (int i = 0; i < 10 && res; ++i) {
                    res &= test_array(gen.rand_vec<arr_t>(sz), func);
                }
            }
            std::cout << "Small arrays: " << result(res) << '\n';
        }
        {
            bool res = true;
            for (int i = 0; i < 100 && res; ++i) {
                for (int j = 0; j < 100; ++j) {
                    size_t sz = gen.rand(100, 10000);
                    res &= test_array(gen.rand_vec<arr_t>(sz), func);
                }
            }
            std::cout << "Random tests: " << result(res) << '\n';
        }
    }
} // namespace testing

#define WRAP(func) [&](auto ...args) {func(args...);}

int main() {
    std::cout << "Number of available threads: " << parlay::num_workers() << "\n\n";
    constexpr Par::split_options F{true, false, false};
    constexpr Par::split_options C{false, true, false};
    constexpr Par::split_options FC{true, true, false};
    constexpr Par::split_options PART{false, false, true};

    testing::test_correctness(WRAP(Seq::quicksort));
    testing::test_correctness(WRAP(Par::quicksort<1024>));
    testing::test_correctness(WRAP((Par::quicksort2<1024>)));
    testing::test_correctness(WRAP((Par::quicksort<1024, true, PART>)));

    std::cout << '\n';

    testing::test_speed(
        testing::Func("seq", WRAP(Seq::quicksort)),
        testing::Func("seq_par", WRAP(Seq::quicksort<true>)),
        testing::Func("par", WRAP(Par::quicksort<1024>)),
        // testing::Func("parc", WRAP((Par::quicksort<1024, false, C>))),
        // testing::Func("parf", WRAP((Par::quicksort<1024, false, F>))),
        // testing::Func("parfc", WRAP((Par::quicksort<1024, false, FC>))),
        testing::Func("parpart", WRAP((Par::quicksort<1024, false, PART>))),
        testing::Func("par2", WRAP(Par::quicksort2<1024>)),
        // testing::Func("par2c", WRAP((Par::quicksort2<1024, C>))),
        // testing::Func("par2f", WRAP((Par::quicksort2<1024, F>))),
        // testing::Func("par2fc", WRAP((Par::quicksort2<1024, FC>))),
        testing::Func("par2part", WRAP((Par::quicksort2<1024, PART>))),
        testing::Func("par3", WRAP((Par::quicksort<1024, true>))),
        // testing::Func("par3c", WRAP((Par::quicksort<1024, true, C>))),
        // testing::Func("par3f", WRAP((Par::quicksort<1024, true, F>))),
        // testing::Func("par3fc", WRAP((Par::quicksort<1024, true, FC>))),
        testing::Func("par3part", WRAP((Par::quicksort<1024, true, PART>))),
        testing::Func("std", WRAP(std::sort)),
        // testing::Func("parlay_internal", [](auto l, auto r){parlay::internal::quicksort(l, r - l, std::less<>{});}),
        testing::Func("parlay", [](auto l, auto r){parlay::sort_inplace(parlay::make_slice(l, r));})
    );
}
