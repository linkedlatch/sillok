"""Runs the sillok evaluation and writes OUT_DIR/results.json.

Usage:
  python3 -I run_eval.py --build BUILD_DIR --models DIR --audio DIR \\
      --device cpu|gpu --out OUT_DIR [--only step,step,...]
Steps: accuracy, switch, korean_speakers, ami, ami_dtw, long, cancel, exit.
Each step's result is saved as soon as it finishes, so a run can be resumed
with --only for the missing steps.
"""
import argparse
import json
import os
import random
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import evaluate  # noqa: E402


def timed(command, env=None):
    """Runs `command` under /usr/bin/time; returns (seconds, peak RSS in MB)."""
    report = "/tmp/sillok-eval-time.txt"
    started = time.monotonic()
    subprocess.run(["/usr/bin/time", "-f", "%M", "-o", report] + command, check=True,
                   env=env, stdout=subprocess.DEVNULL)
    seconds = time.monotonic() - started
    with open(report) as file:
        rss_mb = int(file.read().split()[-1]) / 1024
    return seconds, rss_mb


class Evaluation:
    def __init__(self, args):
        self.args = args
        self.cli = os.path.join(args.build, "apps", "cli", "sillok")
        self.probe = os.path.join(args.build, "tests", "sillok_probe")
        self.sets = os.path.join(args.audio, "sets")
        os.makedirs(args.out, exist_ok=True)
        self.results_path = os.path.join(args.out, "results.json")
        self.results = {}
        if os.path.exists(self.results_path):
            self.results = evaluate.load(self.results_path)

    def save(self, step, result):
        self.results[step] = result
        with open(self.results_path, "w", encoding="utf-8") as file:
            json.dump(self.results, file, ensure_ascii=False, indent=1)
        print(f"[{step}] {json.dumps(result, ensure_ascii=False)}", flush=True)

    def transcribe(self, name, wav, diarize, env=None):
        output = os.path.join(self.args.out, f"{name}.json")
        command = [self.cli, "transcribe", wav, "--models", self.args.models,
                   "--device", self.args.device, "--format", "json", "--quiet",
                   "--output", output]
        if diarize:
            command.append("--diarize")
        seconds, rss = timed(command, env)
        transcript = evaluate.load(output)
        duration = transcript["duration_ms"] / 1000
        return transcript, {"seconds": round(seconds, 1), "rtf": round(seconds / duration, 3),
                            "peak_rss_mb": round(rss), "audio_minutes": round(duration / 60, 1),
                            "devices": {k: v["device"] for k, v in transcript["models"].items()}}

    def set_path(self, name, extension):
        return os.path.join(self.sets, f"{name}.{extension}")

    def accuracy(self):
        result = {}
        for name in ("fleurs_ko", "fleurs_en"):
            transcript, run = self.transcribe(name, self.set_path(name, "wav"), diarize=False)
            reference = evaluate.load(self.set_path(name, "json"))
            result[name] = dict(evaluate.accuracy(reference, transcript), **run)
        return result

    def switch(self):
        transcript, run = self.transcribe("switch", self.set_path("switch", "wav"), diarize=False)
        reference = evaluate.load(self.set_path("switch", "json"))
        return dict(evaluate.switch(reference, transcript), **run)

    def korean_speakers(self):
        name = "korean_two_speakers"
        transcript, run = self.transcribe(name, self.set_path(name, "wav"), diarize=True)
        return dict(evaluate.speakers(evaluate.load(self.set_path(name, "json")), transcript), **run)

    def ami(self, env=None, name="ami"):
        download = os.path.join(self.args.audio, "download")
        wav = os.path.join(download, "ES2004a.Mix-Headset.wav")
        transcript, run = self.transcribe(name, wav, diarize=True, env=env)
        return dict(evaluate.ami(transcript, download, 0.0), **run)

    def ami_dtw(self):
        return self.ami(dict(os.environ, SILLOK_WHISPER_DTW="1"), "ami_dtw")

    def long(self):
        result = {}
        for name in ("long_1h", "long_3h"):
            _, run = self.transcribe(name, self.set_path(name, "wav"), diarize=True)
            result[name] = run
        return result

    def probe_runs(self, mode, trials=10):
        rng = random.Random(7)
        wav = self.set_path("fleurs_en", "wav")
        runs = []
        for _ in range(trials):
            delay = rng.randint(2000, 60000)
            started = time.monotonic()
            process = subprocess.run([self.probe, mode, self.args.models, self.args.device, wav,
                                      str(delay)], capture_output=True, text=True)
            elapsed = time.monotonic() - started
            entry = {"delay_ms": delay, "exit_code": process.returncode,
                     "seconds_after_request": round(elapsed - delay / 1000, 2)}
            if mode == "cancel" and process.returncode == 0:
                entry.update(json.loads(process.stdout.strip().splitlines()[-1]))
            runs.append(entry)
        return runs

    def cancel(self):
        runs = self.probe_runs("cancel")
        latencies = [r["cancel_to_return_ms"] for r in runs if "cancel_to_return_ms" in r]
        return {"runs": runs, "max_ms": max(latencies, default=None),
                "median_ms": sorted(latencies)[len(latencies) // 2] if latencies else None}

    def exit(self):
        runs = self.probe_runs("exit")
        return {"runs": runs, "crashes": sum(1 for r in runs if r["exit_code"] != 0),
                "max_seconds_to_exit": max(r["seconds_after_request"] for r in runs)}


def main():
    parser = argparse.ArgumentParser()
    for flag in ("--build", "--models", "--audio", "--device", "--out"):
        parser.add_argument(flag, required=True)
    parser.add_argument("--only", default="accuracy,switch,korean_speakers,ami,ami_dtw,long,cancel,exit")
    args = parser.parse_args()
    evaluation = Evaluation(args)
    for step in args.only.split(","):
        started = time.monotonic()
        print(f"[{step}] running", flush=True)
        evaluation.save(step, getattr(evaluation, step)())
        print(f"[{step}] took {time.monotonic() - started:.0f} s", flush=True)


if __name__ == "__main__":
    main()
