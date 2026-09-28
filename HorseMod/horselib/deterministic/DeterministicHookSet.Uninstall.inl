bool DeterministicHookSet::Uninstall() noexcept
{
    if (!replay_host_.StopForDestruction()) return false;
    if (!installed_.load(std::memory_order_acquire))
    {
        return true;
    }
    if(callbacks_in_flight_.load(std::memory_order_acquire)) return false;
    // Hooks are removed in the reverse of their installation order.
    if (resolved_hit_consumer_detour_)
        if(resolved_hit_consumer_detour_->isHooked() && !resolved_hit_consumer_detour_->unHook()) return false;
    if (movevm_write_chara_state_short_detour_)
        if(movevm_write_chara_state_short_detour_->isHooked() && !movevm_write_chara_state_short_detour_->unHook()) return false;
    if (movevm_execute_bank_slot_detour_)
        if(movevm_execute_bank_slot_detour_->isHooked() && !movevm_execute_bank_slot_detour_->unHook()) return false;
    if (movevm_transition_author_07_detour_)
        if(movevm_transition_author_07_detour_->isHooked() && !movevm_transition_author_07_detour_->unHook()) return false;
    if (movevm_evaluate_if_detour_)
        if(movevm_evaluate_if_detour_->isHooked() && !movevm_evaluate_if_detour_->unHook()) return false;
    if (gameplay_xorshift96_detour_)
        if(gameplay_xorshift96_detour_->isHooked() && !gameplay_xorshift96_detour_->unHook()) return false;
    if(!UninstallUcrtIatHooks()) return false;
    if (particle_finished_bind_detour_)
        if(particle_finished_bind_detour_->isHooked() && !particle_finished_bind_detour_->unHook()) return false;
    if (particle_spawn_detour_) if(particle_spawn_detour_->isHooked() && !particle_spawn_detour_->unHook()) return false;
    if (battle_audio_append_parameter_detour_)
        if(battle_audio_append_parameter_detour_->isHooked() && !battle_audio_append_parameter_detour_->unHook()) return false;
    if (battle_audio_stop_all_detour_)
        if(battle_audio_stop_all_detour_->isHooked() && !battle_audio_stop_all_detour_->unHook()) return false;
    if (battle_audio_append_command_detour_)
        if(battle_audio_append_command_detour_->isHooked() && !battle_audio_append_command_detour_->unHook()) return false;
    if (battle_audio_register_voice_detour_)
        if(battle_audio_register_voice_detour_->isHooked() && !battle_audio_register_voice_detour_->unHook()) return false;
    if (battle_audio_resolve_chara_cue_detour_)
        if(battle_audio_resolve_chara_cue_detour_->isHooked() && !battle_audio_resolve_chara_cue_detour_->unHook()) return false;
    if (battle_audio_blueprint_publish_detour_)
        if(battle_audio_blueprint_publish_detour_->isHooked() && !battle_audio_blueprint_publish_detour_->unHook()) return false;
    if (battle_audio_tracking_rehash_detour_)
        if(battle_audio_tracking_rehash_detour_->isHooked() && !battle_audio_tracking_rehash_detour_->unHook()) return false;
    if (battle_audio_tracking_insert_detour_)
        if(battle_audio_tracking_insert_detour_->isHooked() && !battle_audio_tracking_insert_detour_->unHook()) return false;
    if (battle_audio_tracking_remove_detour_)
        if(battle_audio_tracking_remove_detour_->isHooked() && !battle_audio_tracking_remove_detour_->unHook()) return false;
    if (battle_audio_phase_changed_detour_)
        if(battle_audio_phase_changed_detour_->isHooked() && !battle_audio_phase_changed_detour_->unHook()) return false;
    if (battle_audio_contact_handler_detour_)
        if(battle_audio_contact_handler_detour_->isHooked() && !battle_audio_contact_handler_detour_->unHook()) return false;
    if (battle_audio_remap_detour_) if(battle_audio_remap_detour_->isHooked() && !battle_audio_remap_detour_->unHook()) return false;
    if (battle_audio_dispatch_detour_)
        if(battle_audio_dispatch_detour_->isHooked() && !battle_audio_dispatch_detour_->unHook()) return false;
    if (stage_break_dispatch_detour_) if(stage_break_dispatch_detour_->isHooked() && !stage_break_dispatch_detour_->unHook()) return false;
    if (stage_break_barrier_detour_) if(stage_break_barrier_detour_->isHooked() && !stage_break_barrier_detour_->unHook()) return false;
    if (stage_break_wall_detour_) if(stage_break_wall_detour_->isHooked() && !stage_break_wall_detour_->unHook()) return false;
    if (callback_executor_detour_)
    {
        if(callback_executor_detour_->isHooked() && !callback_executor_detour_->unHook()) return false;
    }
    if (tutorial_tick_detour_) if(tutorial_tick_detour_->isHooked() && !tutorial_tick_detour_->unHook()) return false;
    if (input_producer_tick_detour_) if(input_producer_tick_detour_->isHooked() && !input_producer_tick_detour_->unHook()) return false;
    if (input_sample_detour_) if(input_sample_detour_->isHooked() && !input_sample_detour_->unHook()) return false;
    if (outer_tick_detour_)
    {
        if(outer_tick_detour_->isHooked() && !outer_tick_detour_->unHook()) return false;
    }
    if (replay_post_tick_detour_)
    {
        if(replay_post_tick_detour_->isHooked() && !replay_post_tick_detour_->unHook()) return false;
    }
    if (frame_fencepost_detour_)
    {
        if(frame_fencepost_detour_->isHooked() && !frame_fencepost_detour_->unHook()) return false;
    }
    active_.store(nullptr, std::memory_order_release);
    if(callbacks_in_flight_.load(std::memory_order_acquire)) return false;
    ClearState();
    installed_.store(false,std::memory_order_release);
    return true;
}
