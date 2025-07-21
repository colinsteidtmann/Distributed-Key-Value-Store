#!/bin/bash

pids=()

cleanup() {
  echo "Stopping servers..."
  for pid in "${pids[@]}"; do
    kill "$pid" 2>/dev/null
  done
  wait
  exit 0
}

trap cleanup SIGINT SIGTERM

for port in {8081..8084}; do
  ./build/Server "$port" &
  pids+=($!)
done

wait
