# Discrete optimization

1. Build. Install a C++ compiler and CMake, then run:

   ```sh
   ./build.sh
   ```

   Pass target names to build only selected problems, for example `./build.sh tsp vrp`.

2. Evaluate. Run the evaluation config for a problem:

   ```sh
   ./evaluate.sh --task tsp
   ```

   Results are saved under `tasks/tsp/results/evaluation`. Use `--jobs N` to set parallelism, `--seed N` for a fixed seed, and `--typst` to print a Typst table.
