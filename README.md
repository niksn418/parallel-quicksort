# parallel-quicksort

This project is using Parlaylib. For building, if cmake can't find your Parlaylib installation path, specify it via `CMAKE_PREFIX_PATH` variable, f.e. ```cmake -DCMAKE_PREFIX_PATH=/home/user/parlaylib```.

Use `PARLAY_NUM_THREADS` environment variable when running `parallel_quicksort` to specify number of treads, f.e. ```PARLAY_NUM_THREADS=4 ./parallel_quicksort```

Description of algorithms:
* `seq`: ordinary sequential quick sort algorithm. Pivot element is selected randomly.
* `seq_par`: like `seq`, but with parallelized recursive calls.
* `par`: like `seq_par`, but also parallelized the splitting process. It splits the array in three parts: [x < pivot), [x == pivot], (pivot < x]. It does that by 3 calls to `parlay::filter`.
* `par2`: like `par`, but splits only in two parts: less than pivot and all other. It does that by 2 calls to `parlay::filter`.
* `par3`: like `par`, but selects two pivoting elements and splits in ranges: [x < pivot1), [pivot1 <= x && x <= pivot2], (pivot2 < x].
* `parpart`: like `par`, but for splitting uses partition algorithm instead of filtering.
* `par2part`: like `par2`, but for splitting uses partition algorithm instead of filtering.
* `par3part`: like `par3`, but for splitting uses partition algorithm instead of filtering.
* `std`: `std::sort`
* `parlay`: `parlay::sort_inplace`

Total running time for sorting random array of $10^8$ 64bit integers 5 times:

|          | 4 threads | 8 threads |
|----------|-----------|-----------|
| seq      | 71.2075s  | 70.382s   |
| seq_par  | 41.3475s  | 27.3882s  |
| par      | 76.1048s  | 64.2641s  |
| parpart  | 55.2669s  | 49.1817s  |
| par2     | 62.9507s  | 53.1886s  |
| par2part | 47.7208s  | 40.7992s  |
| par3     | 60.9763s  | 48.8244s  |
| par3part | 48.2086s  | 40.4854s  |
| std      | 50.9262s  | 50.6392s  |
| parlay   | 15.8852s  | 10.365s   |

This way, on 4 threads, **~1.5x** speedup was achieved with algorithm with **polylog span** (`par2part`/`par3part`), and **~1.72x** speedup with algorithm with **linear span** (`seq_par`).