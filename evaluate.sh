#!/usr/bin/env bash

set -euo pipefail

repo_dir=$(cd "$(dirname "$0")" && pwd)
task=""
config_file=""
binary_override=""
output_override=""
seed_override=""
jobs=4
output_format=text

usage() {
  cat <<'EOF'
Usage: ./evaluate.sh [options] [INSTANCE ...]

Options:
  --task NAME         Evaluate tasks/NAME
  --config PATH       Use a specific evaluation config
  --binary PATH       Use a specific solver binary
  --output-dir PATH   Write solutions and measurements to PATH
  --jobs N            Run N instances in parallel (default: 4)
  --seed N            Pass a fixed RNG seed to every solver run
  --typst             Print the result table as Typst markup
  --help              Show this help

With no INSTANCE arguments, all instances from the evaluation config are run.
EOF
}

require_option_value() {
  if (($# < 2)) || [[ $2 == --* ]]; then
    printf 'missing value for %s\n' "$1" >&2
    exit 1
  fi
}

load_config() {
  if [[ -z $config_file ]]; then
    if [[ -z $task ]]; then
      printf 'either --task or --config is required\n' >&2
      exit 1
    fi
    config_file="$repo_dir/tasks/$task/eval.conf"
  fi
  if [[ ! -f $config_file ]]; then
    printf 'evaluation config not found: %s\n' "$config_file" >&2
    exit 1
  fi
  source "$config_file"
  task_dir=$(cd "$(dirname "$config_file")" && pwd)
  case ${eval_score_direction:-} in
    minimize|maximize) ;;
    *)
      printf 'invalid or missing eval_score_direction in %s\n' "$config_file" >&2
      exit 1
      ;;
  esac
  local instance_count=${#eval_instances[@]}
  if ((instance_count == 0 ||
       ${#eval_three_thresholds[@]} != instance_count ||
       ${#eval_five_thresholds[@]} != instance_count)); then
    printf 'invalid threshold arrays in %s\n' "$config_file" >&2
    exit 1
  fi
}

points_for() {
  local name=$1
  local score=$2
  local index
  for index in "${!eval_instances[@]}"; do
    if [[ $(basename "${eval_instances[index]}") != "$name" ]]; then
      continue
    fi
    awk -v score="$score" -v three="${eval_three_thresholds[index]}" \
        -v direction="$eval_score_direction" \
        -v five="${eval_five_thresholds[index]}" '
      BEGIN {
        if (direction == "maximize") {
          if (score >= five) print 5;
          else if (score >= three) print 3;
          else print 0;
        } else if (score <= five) print 5;
        else if (score <= three) print 3;
        else print 0;
      }
    '
    return
  done
  printf '0'
}

run_one() {
  local instance=$1
  local name
  name=$(basename "$instance")
  local solution_file="$output_dir/$name.solution"
  local time_file="$output_dir/$name.time"
  local row_file="$output_dir/$name.tsv"

  if [[ -n $seed_override ]]; then
    /usr/bin/time -p -o "$time_file" \
      "$binary" "$instance" "$solution_file" --seed "$seed_override"
  else
    /usr/bin/time -p -o "$time_file" "$binary" "$instance" "$solution_file"
  fi

  local score
  score=$(awk '$1 == "score" { print $2; exit }' "$solution_file")
  if [[ -z $score ]]; then
    printf 'solver did not write a score for %s\n' "$name" >&2
    exit 1
  fi
  local user_time
  user_time=$(awk '$1 == "user" { print $2; exit }' "$time_file")
  local points
  points=$(points_for "$name" "$score")
  printf '%s\t%s\t%s\t%s\n' "$name" "$score" "$user_time" "$points" > "$row_file"
}

if [[ ${1:-} == "--run-one" ]]; then
  binary=$2
  output_dir=$3
  config_file=$4
  seed_override=$5
  load_config
  run_one "$6"
  exit 0
fi

instances=()
while (($#)); do
  case $1 in
    --task) require_option_value "$@"; task=$2; shift 2 ;;
    --config) require_option_value "$@"; config_file=$2; shift 2 ;;
    --jobs) require_option_value "$@"; jobs=$2; shift 2 ;;
    --binary) require_option_value "$@"; binary_override=$2; shift 2 ;;
    --output-dir) require_option_value "$@"; output_override=$2; shift 2 ;;
    --seed) require_option_value "$@"; seed_override=$2; shift 2 ;;
    --typst) output_format=typst; shift ;;
    --help) usage; exit 0 ;;
    --*) printf 'unknown option: %s\n' "$1" >&2; usage >&2; exit 1 ;;
    *) instances+=("$1"); shift ;;
  esac
done

if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
  printf 'jobs must be a positive integer: %s\n' "$jobs" >&2
  exit 1
fi

load_config
binary=${binary_override:-"$repo_dir/$eval_binary"}
output_dir=${output_override:-"$task_dir/$eval_output_dir"}

if ((${#instances[@]} == 0)); then
  for instance in "${eval_instances[@]}"; do
    instances+=("$task_dir/$instance")
  done
fi

if [[ ! -x $binary ]]; then
  printf 'solver binary not found or not executable: %s\n' "$binary" >&2
  exit 1
fi

for instance in "${instances[@]}"; do
  if [[ ! -f $instance ]]; then
    printf 'instance not found: %s\n' "$instance" >&2
    exit 1
  fi
done

mkdir -p "$output_dir"

printf '%s\0' "${instances[@]}" |
  xargs -0 -n 1 -P "$jobs" "$0" --run-one "$binary" "$output_dir" "$config_file" "$seed_override"

table="$output_dir/scores.tsv"
printf 'instance\tscore\tcpu_seconds\tpoints\n' > "$table"
for instance in "${instances[@]}"; do
  cat "$output_dir/$(basename "$instance").tsv" >> "$table"
done

total_points=$(awk -F '\t' 'NR > 1 { total += $4 } END { print total + 0 }' "$table")
max_points=$(( ${#instances[@]} * 5 ))

if [[ $output_format == typst ]]; then
  printf '#table(\n'
  printf '  columns: (1fr, auto, auto, auto),\n'
  printf '  table.header([*Instance*], [*Score*], [*CPU time, s*], [*Points*]),\n'
  while IFS=$'\t' read -r instance score cpu_seconds points; do
    [[ $instance == instance ]] && continue
    printf '  [`%s`], [%s], [%s], [%s],\n' \
      "$instance" "$score" "$cpu_seconds" "$points"
  done < "$table"
  printf ')\n\n'
  printf '*Total: %s / %s*\n' "$total_points" "$max_points"
else
  printf '%-24s %20s %14s %8s\n' 'INSTANCE' 'SCORE' 'CPU, S' 'POINTS'
  while IFS=$'\t' read -r instance score cpu_seconds points; do
    [[ $instance == instance ]] && continue
    printf '%-24s %20s %14s %8s\n' \
      "$instance" "$score" "$cpu_seconds" "$points"
  done < "$table"
  printf '\nTotal: %s / %s\n' "$total_points" "$max_points"
fi
