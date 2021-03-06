#include <Systems/AnimationSystem.h>

#include <Games/Animation/TESActionData.h>
#include <Games/Animation/ActorMediator.h>

#include <Games/References.h>

#include <Forms/BGSAction.h>
#include <Misc/ActorProcessManager.h>
#include <Misc/MiddleProcess.h>

#include <Messages/ClientReferencesMoveRequest.h>

#include <Components.h>
#include <World.h>
#include "ViewBuffer.hpp"

extern thread_local const char* g_animErrorCode;

void AnimationSystem::Update(World& aWorld, Actor* apActor, RemoteAnimationComponent& aAnimationComponent, const uint64_t aTick) noexcept
{
    auto& actions = aAnimationComponent.TimePoints;

    const auto it = std::begin(actions);
    if (it != std::end(actions) && it->Tick <= aTick)
    {
        const auto& first = *it;

        const auto actionId = first.ActionId;
        const auto targetId = first.TargetId;

        const auto pAction = RTTI_CAST(TESForm::GetById(actionId), TESForm, BGSAction);
        const auto pTarget = RTTI_CAST(TESForm::GetById(targetId), TESForm, TESObjectREFR);

        apActor->actorState.flags1 = first.State1;
        apActor->actorState.flags2 = first.State2;

        apActor->LoadAnimationVariables(first.Variables);

        aAnimationComponent.LastRanAction = first;

        // Play the animation
        TESActionData actionData(first.Type & 0x3, apActor, pAction, pTarget);
        actionData.eventName = BSFixedString(first.EventName.c_str());
        actionData.idleForm = RTTI_CAST(TESForm::GetById(first.IdleId), TESForm, TESIdleForm);
        actionData.someFlag = ((first.Type & 0x4) != 0) ? 1 : 0;

        const auto result = ActorMediator::Get()->ForceAction(&actionData);

        actions.pop_front();
    }
}

void AnimationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    aWorld.emplace_or_replace<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::Clean(World& aWorld, const entt::entity aEntity) noexcept
{
    aWorld.remove_if_exists<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::AddAction(RemoteAnimationComponent& aAnimationComponent, const std::string& acActionDiff) noexcept
{
    auto itor = std::begin(aAnimationComponent.TimePoints);
    const auto end = std::cend(aAnimationComponent.TimePoints);

    auto& lastProcessedAction = aAnimationComponent.LastProcessedAction;

    TiltedPhoques::ViewBuffer buffer((uint8_t*)acActionDiff.data(), acActionDiff.size());
    Buffer::Reader reader(&buffer);

    lastProcessedAction.ApplyDifferential(reader);

    aAnimationComponent.TimePoints.push_back(lastProcessedAction);
}

void AnimationSystem::Serialize(World& aWorld, ClientReferencesMoveRequest& aMovementSnapshot, LocalComponent& localComponent, LocalAnimationComponent& animationComponent, FormIdComponent& formIdComponent)
{
    const auto pForm = TESForm::GetById(formIdComponent.Id);
    const auto pActor = RTTI_CAST(pForm, TESForm, Actor);
    if (!pActor)
        return;

    auto& update = aMovementSnapshot.Updates[localComponent.Id];
    auto& movement = update.UpdatedMovement;

    movement.Position = pActor->position;

    movement.Rotation.X = pActor->rotation.m_x;
    movement.Rotation.Y = pActor->rotation.m_z;

    pActor->SaveAnimationVariables(movement.Variables);

    if (pActor->processManager && pActor->processManager->middleProcess)
    {
        movement.Direction = pActor->processManager->middleProcess->direction;
    }

    for (auto& entry : animationComponent.Actions)
    {
        update.ActionEvents.push_back(entry);
    }

    auto latestAction = animationComponent.GetLatestAction();

    if (latestAction)
        localComponent.CurrentAction = latestAction.MoveResult();

    animationComponent.Actions.clear();
}

bool AnimationSystem::Serialize(World& aWorld, const ActionEvent& aActionEvent, const ActionEvent& aLastProcessedAction, std::string* apData)
{
    uint32_t actionBaseId = 0;
    uint32_t actionModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.ActionId, actionModId, actionBaseId))
        return false;

    uint32_t targetBaseId = 0;
    uint32_t targetModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.TargetId, targetModId, targetBaseId))
        return false;

    uint8_t scratch[1 << 12];
    TiltedPhoques::ViewBuffer buffer(scratch, std::size(scratch));
    Buffer::Writer writer(&buffer);
    aActionEvent.GenerateDifferential(aLastProcessedAction, writer);

    apData->assign(buffer.GetData(), buffer.GetData() + writer.Size());

    return true;
}
