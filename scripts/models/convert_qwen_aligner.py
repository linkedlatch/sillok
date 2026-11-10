"""Converts Qwen3-ForcedAligner-0.6B for the Qwen backend.

Usage: python3 scripts/models/convert_qwen_aligner.py MODEL_DIR OUT_DIR
MODEL_DIR is Qwen/Qwen3-ForcedAligner-0.6B from Hugging Face (Apache 2.0);
the environment needs qwen-asr==0.0.6 (which registers its model type with
transformers), torch and sentencepiece. llama.cpp's converter in
third_party/llama.cpp writes, into OUT_DIR,
  qwen3-forced-aligner-0.6b-q8_0.gguf       the language model, Q8_0
  mmproj-qwen3-forced-aligner-0.6b-f16.gguf the audio encoder, F16
The aligner's output layer gives one of 5000 classes of 80 ms instead of a
token. llama.cpp takes an output layer as wide as the vocabulary, so the
classes become its first rows and the rest are zero: the logits of a
<timestamp> slot then hold the classes first. The class count and length go
in "qwen3_forced_aligner.classes" and "qwen3_forced_aligner.segment_ms".
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LLAMA = os.path.join(ROOT, "third_party", "llama.cpp")
sys.path.insert(0, LLAMA)
sys.path.insert(1, os.path.join(LLAMA, "gguf-py"))

import torch  # noqa: E402
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


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    model_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(model_dir, "config.json"), encoding="utf-8") as file:
        config = json.load(file)
    thinker = config["thinker_config"]
    classes = thinker["classify_num"]
    vocab = thinker["text_config"]["vocab_size"]

    heads = []
    original_tensors = qwen3vl.Qwen3ASRTextModel.modify_tensors
    original_parameters = qwen3vl.Qwen3ASRTextModel.set_gguf_parameters

    def modify_tensors(self, data_torch, name, bid):
        if "lm_head" in name:
            if tuple(data_torch.shape) != (classes, thinker["text_config"]["hidden_size"]):
                sys.exit(f"unexpected timestamp head shape {tuple(data_torch.shape)}")
            heads.append(name)
            padded = torch.zeros((vocab, data_torch.shape[1]), dtype=data_torch.dtype)
            padded[:classes] = data_torch
            return original_tensors(self, padded, name, bid)
        return original_tensors(self, data_torch, name, bid)

    def set_gguf_parameters(self):
        original_parameters(self)
        self.gguf_writer.add_uint32("qwen3_forced_aligner.classes", classes)
        self.gguf_writer.add_uint32("qwen3_forced_aligner.segment_ms", config["timestamp_segment_time"])

    qwen3vl.Qwen3ASRTextModel.modify_tensors = modify_tensors
    qwen3vl.Qwen3ASRTextModel.set_gguf_parameters = set_gguf_parameters
    for extra, name, outtype in (([], TEXT, "q8_0"), (["--mmproj"], MMPROJ, "f16")):
        sys.argv = ["convert_hf_to_gguf.py", model_dir, *extra, "--outtype", outtype,
                    "--outfile", os.path.join(out_dir, name)]
        convert_hf_to_gguf.main()
    if len(heads) != 1:
        sys.exit(f"expected one timestamp head, found {len(heads)}")
    print("wrote", TEXT, "and", MMPROJ, "to", out_dir)


if __name__ == "__main__":
    main()
