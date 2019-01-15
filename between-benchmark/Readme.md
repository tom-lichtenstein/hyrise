# Range Optimization Benchmarks (BETWEEN)

* build and run tpchTableGenerator
* copy the files into this directory
  * e.g. inside cmake directory: mv \*csv\* ../benchmark
* from inside this directory run: ../<cmake build directory>/hyriseBenchmarkFileBased <benchmark-config>.json
  * e.g. ../cmake-build-releaseepic/hyriseBenchmarkFileBased original-unencoded.json

## Configs explained

The configs will make the benchmark runner first load all available tables in the current directory (*.tbl or *.csv).
After that, it will run all queries in the current directory (*.sql)
