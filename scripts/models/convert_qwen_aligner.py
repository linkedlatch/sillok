"""Converts Qwen3-ForcedAligner-0.6B for the Qwen backend.

Usage: python3 scripts/models/convert_qwen_aligner.py MODEL_DIR OUT_DIR
MODEL_DIR is Qwen/Qwen3-ForcedAligner-0.6B from Hugging Face (Apache 2.0);
the environment needs qwen-asr==0.0.6 (which registers its model type with
transformers), torch and sentencepiece. llama.cpp's converter in
third_party/llama.cpp writes, into OUT_DIR,
  qwen3-forced-aligner-0.6b-q8_0.gguf       the text model, Q8_0
  mmproj-qwen3-forced-aligner-0.6b-f16.gguf the audio encoder, F16
and this script
  qwen3-forced-aligner-0.6b-head-f32.gguf   the timestamp head
The aligner's output layer gives one of 5000 classes of 80 ms instead of a
token, so llama.cpp cannot load it as the text model's output: it is written
apart as the tensor "timestamp_head.weight" (1024 x 5000, F32), with the
class length in "timestamp.segment_ms" and the slot token in
"timestamp.token_id". The text model's own output layer is the embedding
table (tied), which the backend does not use.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LLAMA = os.path.join(ROOT, "third_party", "llama.cpp")
sys.path.insert(0, LLAMA)
sys.path.insert(1, os.path.join(LLAMA, "gguf-py"))

import gguf  # noqa: E402
import numpy as np  # noqa: E402
from transformers import AutoConfig, AutoModel, AutoProcessor  # noqa: E402
from qwen_asr.core.transformers_backend import (  # noqa: E402
    Qwen3ASRConfig, Qwen3ASRForConditionalGeneration, Qwen3ASRProcessor)

AutoConfig.register("qwen3_asr", Qwen3ASRConfig)
AutoModel.register(Qwen3ASRConfig, Qwen3ASRForConditionalGeneration)
AutoProcessor.register(Qwen3ASRConfig, Qwen3ASRProcessor)

import convert_hf_to_gguf  # noqa: E402
from conversion import qwen3vl  # noqa: E402

TEXT = "qwen3-forced-aligner-0.6b-q8_0.gguf"
MMPROJ = "mmproj-qwen3-forced-aligner-0.6b-f16.gguf"
HEAD = "qwen3-forced-aligner-0.6b-head-f32.gguf"


def write_head(head, config, path):
    thinker = config["thinker_config"]
    if head.shape != (thinker["classify_num"], thinker["text_config"]["hidden_size"]):
        sys.exit(f"unexpected timestamp head shape {head.shape}")
    writer = gguf.GGUFWriter(path, arch="qwen3_forced_aligner_head")
    writer.add_uint32("timestamp.segment_ms", config["timestamp_segment_time"])
    writer.add_uint32("timestamp.token_id", config["timestamp_token_id"])
    writer.add_tensor("timestamp_head.weight", head.astype(np.float32))
    writer.write_header_to_file()
    writer.write_kv_data_to_file()
    writer.write_tensors_to_file()
    writer.close()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    model_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(model_dir, "config.json"), encoding="utf-8") as file:
        config = json.load(file)

    heads = []
    original = qwen3vl.Qwen3ASRTextModel.modify_tensors

    def modify_tensors(self, data_torch, name, bid):
        if "lm_head" in name:
            heads.append(data_torch.float().numpy())
            return []
        return original(self, data_torch, name, bid)

    qwen3vl.Qwen3ASRTextModel.modify_tensors = modify_tensors
    for extra, name, outtype in (([], TEXT, "q8_0"), (["--mmproj"], MMPROJ, "f16")):
        sys.argv = ["convert_hf_to_gguf.py", model_dir, *extra, "--outtype", outtype,
                    "--outfile", os.path.join(out_dir, name)]
        convert_hf_to_gguf.main()
    if len(heads) != 1:
        sys.exit(f"expected one timestamp head, found {len(heads)}")
    write_head(heads[0], config, os.path.join(out_dir, HEAD))
    print("wrote", ", ".join((TEXT, MMPROJ, HEAD)), "to", out_dir)


if __name__ == "__main__":
    main()
