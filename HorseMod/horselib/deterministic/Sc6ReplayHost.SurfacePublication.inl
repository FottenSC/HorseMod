// Actual render-thread publication cases, also compiled by the ownership fixture.
    case SurfaceCommand::RetainCheckpoint:
        result = self->held_surface_ && surface.retain_replay_surface(self->held_surface_->image);
        if (self->held_surface_) self->held_surface_->retained = result;
        // Record the image only after the render thread actually retained it.
        // Execution handoff alone does not replace the installed A display.
        if (result && self->historical_restore_ && self->historical_restore_->surface_dirty)
            self->historical_restore_->presented_surface = self->held_surface_;
        break;
    case SurfaceCommand::InstallCheckpoint:
        if (self->historical_restore_ && self->historical_restore_->presented_surface)
        {
            auto& transaction = *self->historical_restore_;
            result = surface.install_replay_surface(transaction.presented_surface->image, transaction.undo_surface, transaction.execution!=nullptr);
            transaction.surface_dirty = result;
            transaction.display_started=result && transaction.execution!=nullptr;
        }
        break;
    case SurfaceCommand::PublishCheckpoint:
        if(self->historical_restore_) {
            auto& transaction=*self->historical_restore_;
            if(transaction.presented_surface)surface.publish_replay_controls(transaction.presented_surface->tick,false,false,false);
            result=transaction.execution && transaction.surface_dirty && transaction.presented_surface
                && surface.publish_replay_surface(&transaction.presented_surface->image);
            transaction.display_complete=result;
        }
        break;
    case SurfaceCommand::FinishDisplayCommit:
    case SurfaceCommand::FinishDisplayRecovery:
        if(self->historical_restore_) {
            auto& transaction=*self->historical_restore_;
            if(self->surface_command_==SurfaceCommand::FinishDisplayRecovery)
                surface.publish_replay_controls(transaction.witness.original_tick,false,false,false);
            result=transaction.execution && surface.finish_replay_display_transaction(
                self->surface_command_==SurfaceCommand::FinishDisplayCommit);
            transaction.display_finished=result;
        }
        break;
    case SurfaceCommand::UndoCheckpoint:
        if (self->historical_restore_)
        {
            auto& transaction = *self->historical_restore_;
            const auto* installed = transaction.presented_surface.get();
            result = transaction.surface_dirty && installed && surface.undo_replay_surface(
                installed->image, transaction.undo_surface);
            if (result) transaction.surface_dirty = false;
        }
        break;
