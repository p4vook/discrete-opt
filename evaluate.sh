#!/usr/bin/env bash

set -euo pipefail

repo_dir=$(cd "$(dirname "$0")" && pwd)
task=setcover
config_file=""
binary_override=""
output_override=""
seed_override=""
jobs=4

load_config() {
  if [[ -z $config_file ]]; then
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
    --task) task=$2; shift 2 ;;
    --config) config_file=$2; shift 2 ;;
    --jobs) jobs=$2; shift 2 ;;
    --binary) binary_override=$2; shift 2 ;;
    --output-dir) output_override=$2; shift 2 ;;
    --seed) seed_override=$2; shift 2 ;;
    --help)
      printf 'usage: %s [--task NAME] [--config PATH] [--jobs N] [--binary PATH] [--output-dir PATH] [--seed N] [INSTANCE ...]\n' "$0"
      exit 0
      ;;
    *) instances+=("$1"); shift ;;
  esac
done

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
{
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
}
