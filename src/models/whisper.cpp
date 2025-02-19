#include "../generators.h"
#include "whisper.h"

namespace Generators {

Whisper_Model::Whisper_Model(std::unique_ptr<Config> config, OrtEnv& ort_env, const ProviderOptions* provider_options)
    : Model{std::move(config), provider_options} {
  session_decoder_ = OrtSession::Create(ort_env, (config_->config_path / config_->model.decoder).c_str(), session_options_.get());
  session_encoder_ = OrtSession::Create(ort_env, (config_->config_path / config_->model.encoder_decoder_init).c_str(), session_options_.get());

  InitDeviceAllocator(*session_decoder_);
}

std::unique_ptr<State> Whisper_Model::CreateState(RoamingArray<int32_t> sequence_lengths, const SearchParams& params) {
  return std::make_unique<Whisper_State>(*this, sequence_lengths, params);
}

Whisper_State::Whisper_State(Whisper_Model& model, RoamingArray<int32_t> sequence_lengths_unk, const SearchParams& search_params)
    : State{search_params},
      model_{model} {
  auto& inputs = const_cast<SearchParams::Whisper&>(std::get<SearchParams::Whisper>(search_params.inputs));

  auto encoder_input_ids = model_.ExpandInputs(inputs.input_features, search_params_.num_beams);
  encoder_hidden_states_ = OrtValue::CreateTensor<float>(*model_.allocator_device_, std::array<int64_t, 3>{decoder_input_ids_.GetShape()[0], 1500, 384});

  auto sequence_lengths = sequence_lengths_unk.GetCPU();
  for (int i = 0; i < decoder_input_ids_.GetShape()[0]; i++) {
    sequence_lengths[i] = static_cast<int32_t>(search_params_.sequence_length);
  }

  input_names_.push_back("encoder_input_ids");
  inputs_.push_back(encoder_input_ids.get());
  decoder_input_ids_.name_ = "decoder_input_ids";
  decoder_input_ids_.Add();

  logits_.Add();
  output_names_.push_back("encoder_hidden_states");
  outputs_.push_back(encoder_hidden_states_.get());
  kv_cache_.AddEncoder();
  cross_cache_.AddOutputs();

  State::Run(*model_.session_encoder_);

  ClearIO();

  decoder_input_ids_.name_ = "input_ids";  // Set back to default name, since we overrode it above in the encoder step
  decoder_input_ids_.Add();
  logits_.Add();
  kv_cache_.Add();
  cross_cache_.AddInputs();
}

RoamingArray<float> Whisper_State::Run(int current_length, RoamingArray<int32_t> next_tokens, RoamingArray<int32_t> next_indices) {
  if (first_run_) {
    first_run_ = false;
  } else {
    UpdateInputs(next_tokens, next_indices, current_length);
    State::Run(*model_.session_decoder_);
  }
  return logits_.Get();
}

void Whisper_State::UpdateInputs(const RoamingArray<int32_t>& next_tokens, RoamingArray<int32_t> beam_indices, int current_length) {
  decoder_input_ids_.Update(next_tokens);
  logits_.Update();
  kv_cache_.Update(beam_indices.GetCPU(), current_length);
}

}  // namespace Generators
