// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

// The implementation is based on huggingface transformers generation_beam_search.py
namespace Generators {

struct HypothesisScore {
  std::span<const int32_t> hypothesis;
  float score;
};

struct BeamHypotheses {
  // As these are constructed as an uninitialized array of memory, we need an Init method
  void Init(float length_penalty, std::span<HypothesisScore> beams);

  // Add a new hypothesis
  void Add(std::span<const int32_t> hypothesis, float sum_logprobs);

  // Return true if this beats the worst score in the hypothesis
  bool CanImprove(float best_sum_logprobs, int current_length) const;

  // Output results
  void Output(size_t top_k,                              // number of sequences to return
              size_t max_length,                         // max sequence length
              std::span<int32_t> sequences,              // buffer with pad token, shape (num_return_sequences, max_length)
              std::span<float> sequences_scores) const;  // buffer for sequence scores, with shape (num_return_sequences)

  std::span<HypothesisScore> beams_;  // Beam width sized array of hypotheses, sorted by highest scoring
  int beams_used_;                    // Number of elements used in beams_
  float length_penalty_;
  bool done_;
};

struct BeamSearchScorer {
  BeamSearchScorer(const SearchParams& parameters);

  void Process(Sequences& sequences,
               std::span<const float> next_scores,
               std::span<const int32_t> next_tokens,
               std::span<const int32_t> next_indices);

  void Finalize(Sequences& sequences,
                size_t num_return_sequences,
                cpu_span<int32_t> output_sequences,
                cpu_span<float> output_sequence_scores);

  bool IsDone() const { return not_done_count_ == 0; }

  cpu_span<float> GetNextScores() { return next_beam_scores_; }
  cpu_span<int32_t> GetNextTokens() { return next_beam_tokens_; }
  cpu_span<int32_t> GetNextIndicesCPU() { return next_beam_indices_; }

 private:
  int batch_size_;
  int num_beams_;
  int max_length_;
  int pad_token_id_;
  int eos_token_id_;
  bool early_stopping_;
  int not_done_count_;  // When zero, every batch entry is done (starts at batch_size_)

  std::unique_ptr<float[]> next_beam_scores_ptr_;
  cpu_span<float> next_beam_scores_;

  std::unique_ptr<int32_t[]> next_beam_tokens_ptr_;
  cpu_span<int32_t> next_beam_tokens_;

  std::unique_ptr<int32_t[]> next_beam_indices_ptr_;
  cpu_span<int32_t> next_beam_indices_;

  std::unique_ptr<int32_t[]> hypothesis_buffer_ptr_;  // Allocated buffer to hold all hypotheses
  std::span<int32_t> hypothesis_buffer_;              // Span of the allocated buffer
  int hypothesis_buffer_used_{};                      // Offset of available buffer, or length of used buffer.

  std::unique_ptr<HypothesisScore[]> hypothesis_scores_ptr_;  // num_beams_ * batch_size_, divided into num_beams_ chunks per BeamHypothesis in beam_hyps_
  std::unique_ptr<BeamHypotheses[]> beam_hyps_ptr_;
  std::span<BeamHypotheses> beam_hyps_;  // Shape is batch_size_
};

}  // namespace Generators