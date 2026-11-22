// RenderSystem: how a registry's entities become the commands of a render pass. It needs no window or backend -- the
// pass is only data -- so each case fills a registry, updates a pass from it, and reads the pass.

#include <Backends/RenderPass.hpp>
#include <ECS/Components.hpp>
#include <Systems/RenderSystem.hpp>
#include <gtest/gtest.h>

#include <cmath>

namespace {

using BRITE::Primitive3DComponent;
using BRITE::Primitive3DType;
using BRITE::RenderPass;
using BRITE::RenderSystem;
using BRITE::SpriteComponent;
using BRITE::TransformComponent;

constexpr float PI = 3.14159265358979323846f;

SpriteComponent MakeSprite() {
    SpriteComponent sprite;
    sprite.Material.AlbedoTint = {10, 20, 30, 40};
    sprite.SourceRect = {1.0f, 2.0f, 3.0f, 4.0f};
    sprite.DestRect = {5.0f, 6.0f, 20.0f, 10.0f};
    sprite.Origin = {7.0f, 8.0f};
    return sprite;
}

} // namespace

TEST(RenderSystem, AnEmptyRegistryAddsNothingToAPass) {
    entt::registry registry;
    RenderPass pass;

    RenderSystem::Update(registry, pass);

    EXPECT_TRUE(pass.Primitive3DCommands.empty());
    EXPECT_TRUE(pass.SpriteCommands.empty());
    EXPECT_EQ(pass.Camera, nullptr);
    EXPECT_EQ(pass.Camera3DPtr, nullptr);
}

TEST(RenderSystem, ACameraGivenIsTheCameraOfThePassAndNoneGivenLeavesTheOneItHad) {
    entt::registry registry;
    BRITE::Camera2D camera2d{};
    BRITE::Camera3D camera3d{};

    RenderPass given;
    RenderSystem::Update(registry, given, &camera2d, &camera3d);
    EXPECT_EQ(given.Camera, &camera2d);
    EXPECT_EQ(given.Camera3DPtr, &camera3d);

    RenderPass keeping;
    BRITE::Camera2D other2d{};
    BRITE::Camera3D other3d{};
    keeping.Camera = &other2d;
    keeping.Camera3DPtr = &other3d;
    RenderSystem::Update(registry, keeping);
    EXPECT_EQ(keeping.Camera, &other2d);
    EXPECT_EQ(keeping.Camera3DPtr, &other3d);
}

TEST(RenderSystem, AnEnvironmentInTheRegistryIsTheEnvironmentOfThePass) {
    entt::registry registry;
    BRITE::EnvironmentComponent& environment = registry.emplace<BRITE::EnvironmentComponent>(registry.create());
    environment.Map.Cubemap = 7;
    environment.DrawSkybox = false;

    RenderPass pass;
    ASSERT_TRUE(pass.DrawSkybox) << "a pass draws its skybox unless told otherwise";
    RenderSystem::Update(registry, pass);

    EXPECT_EQ(pass.Environment.Cubemap, 7u);
    EXPECT_FALSE(pass.DrawSkybox);
}

TEST(RenderSystem, WithNoEnvironmentInTheRegistryThePassKeepsItsOwn) {
    entt::registry registry;
    RenderPass pass;
    pass.Environment.Cubemap = 9;
    pass.DrawSkybox = false;

    RenderSystem::Update(registry, pass);

    EXPECT_EQ(pass.Environment.Cubemap, 9u);
    EXPECT_FALSE(pass.DrawSkybox);
}

TEST(RenderSystem, APrimitiveWithATransformIsADrawCommandThatCarriesBoth) {
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity, BRITE::Vector3{1.0f, 2.0f, 3.0f},
                                         BRITE::Quaternion{0.0f, 0.5f, 0.0f, 0.5f}, BRITE::Vector3{2.0f, 2.0f, 2.0f});
    Primitive3DComponent& primitive = registry.emplace<Primitive3DComponent>(entity);
    primitive.Type = Primitive3DType::SphereWires;
    primitive.Size = {4.0f, 5.0f, 6.0f};
    primitive.Tint = {1, 2, 3, 4};

    RenderPass pass;
    RenderSystem::Update(registry, pass);

    ASSERT_EQ(pass.Primitive3DCommands.size(), 1u);
    const BRITE::Primitive3DDrawCommand& command = pass.Primitive3DCommands[0];
    EXPECT_EQ(command.Type, Primitive3DType::SphereWires);
    EXPECT_EQ(command.Position.x, 1.0f);
    EXPECT_EQ(command.Position.y, 2.0f);
    EXPECT_EQ(command.Position.z, 3.0f);
    EXPECT_EQ(command.Rotation.y, 0.5f);
    EXPECT_EQ(command.Rotation.w, 0.5f);
    EXPECT_EQ(command.Scale.x, 2.0f);
    EXPECT_EQ(command.Size.x, 4.0f);
    EXPECT_EQ(command.Size.y, 5.0f);
    EXPECT_EQ(command.Size.z, 6.0f);
    EXPECT_EQ(command.Tint.r, 1);
    EXPECT_EQ(command.Tint.a, 4);
}

TEST(RenderSystem, APrimitiveOrASpriteWithNoTransformIsNotDrawn) {
    entt::registry registry;
    registry.emplace<Primitive3DComponent>(registry.create());
    registry.emplace<SpriteComponent>(registry.create(), MakeSprite());
    registry.emplace<TransformComponent>(registry.create());

    RenderPass pass;
    RenderSystem::Update(registry, pass);

    EXPECT_TRUE(pass.Primitive3DCommands.empty());
    EXPECT_TRUE(pass.SpriteCommands.empty());
}

TEST(RenderSystem, EveryPrimitiveAndSpriteIsOneCommand) {
    entt::registry registry;
    for (int i = 0; i < 3; ++i) {
        const entt::entity entity = registry.create();
        registry.emplace<TransformComponent>(entity);
        registry.emplace<Primitive3DComponent>(entity);
    }
    for (int i = 0; i < 2; ++i) {
        const entt::entity entity = registry.create();
        registry.emplace<TransformComponent>(entity);
        registry.emplace<SpriteComponent>(entity, MakeSprite());
    }

    RenderPass pass;
    RenderSystem::Update(registry, pass);

    EXPECT_EQ(pass.Primitive3DCommands.size(), 3u);
    EXPECT_EQ(pass.SpriteCommands.size(), 2u);
}

TEST(RenderSystem, ASpriteIsDrawnAtItsTransformScaledByIt) {
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity, BRITE::Vector2{100.0f, 200.0f}, 0.0f, BRITE::Vector2{3.0f, 0.5f});
    registry.emplace<SpriteComponent>(entity, MakeSprite());

    RenderPass pass;
    RenderSystem::Update(registry, pass);

    ASSERT_EQ(pass.SpriteCommands.size(), 1u);
    const BRITE::SpriteDrawCommand& command = pass.SpriteCommands[0];
    EXPECT_EQ(command.DestRect.x, 105.0f) << "the sprite's own offset added to the position";
    EXPECT_EQ(command.DestRect.y, 206.0f);
    EXPECT_EQ(command.DestRect.width, 60.0f) << "its size times the scale";
    EXPECT_EQ(command.DestRect.height, 5.0f);
    EXPECT_FLOAT_EQ(command.RotationDeg, 0.0f);
}

TEST(RenderSystem, ASpriteKeepsTheRestOfWhatItWasGiven) {
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity);
    registry.emplace<SpriteComponent>(entity, MakeSprite());

    RenderPass pass;
    RenderSystem::Update(registry, pass);

    ASSERT_EQ(pass.SpriteCommands.size(), 1u);
    const BRITE::SpriteDrawCommand& command = pass.SpriteCommands[0];
    EXPECT_EQ(command.Material.AlbedoTint.r, 10);
    EXPECT_EQ(command.Material.AlbedoTint.a, 40);
    EXPECT_EQ(command.SourceRect.x, 1.0f);
    EXPECT_EQ(command.SourceRect.height, 4.0f);
    EXPECT_EQ(command.Origin.x, 7.0f);
    EXPECT_EQ(command.Origin.y, 8.0f);
}

TEST(RenderSystem, ASpritesRotationIsTheDegreesAboutZeroOfItsTransformsQuaternion) {
    for (const float degrees : {0.0f, 30.0f, 90.0f, -45.0f, 170.0f}) {
        entt::registry registry;
        const entt::entity entity = registry.create();
        registry.emplace<TransformComponent>(entity, BRITE::Vector2{0.0f, 0.0f}, degrees);
        registry.emplace<SpriteComponent>(entity, MakeSprite());

        RenderPass pass;
        RenderSystem::Update(registry, pass);

        ASSERT_EQ(pass.SpriteCommands.size(), 1u) << degrees << " degrees";
        EXPECT_NEAR(pass.SpriteCommands[0].RotationDeg, degrees, 1e-3f) << degrees << " degrees";
    }
}

TEST(RenderSystem, AnUpdateAddsToAPassAndLeavesWhatItAlreadyHeld) {
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity);
    registry.emplace<Primitive3DComponent>(entity);

    RenderPass pass;
    pass.Primitive3DCommands.push_back(
        {Primitive3DType::Grid, {}, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, BRITE::White});
    RenderSystem::Update(registry, pass);

    ASSERT_EQ(pass.Primitive3DCommands.size(), 2u);
    EXPECT_EQ(pass.Primitive3DCommands[0].Type, Primitive3DType::Grid) << "what the pass held stays first";
    EXPECT_EQ(pass.Primitive3DCommands[1].Type, Primitive3DType::Cube);
}
