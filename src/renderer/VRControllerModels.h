// license:GPLv3+

#pragma once

#if defined(ENABLE_XR)

#include "math/matrix.h"
#include "parts/Material.h"

class BaseTexture;
class MeshBuffer;
class Renderer;
class RenderDevice;
class VRDevice;

// Draws the controller models given by the OpenXR runtime (see VRDevice::GetControllerModels): their glTF assets are turned into meshes once,
// then each frame every node is placed with the pose of the model and the poses of its animated parts (buttons, triggers, thumbsticks).
//
// An asset is read (glTF parsed, textures decoded) on a thread of its own, once per process: reading the two Steam Frame controllers took
// 0.5 and 0.66 s on the thread that prepares the frames, at the start of every table, and froze the headset's image for 1.5 s each time
// (2026-11-14). The read models are kept for the next tables (the runtime gives the same assets); each table only makes their mesh buffers.
// The controllers show once their asset is read, a moment after the first table starts.
class VRControllerModels final
{
public:
   explicit VRControllerModels(RenderDevice* rd);
   ~VRControllerModels();

   // Called while preparing a frame, with the room space reference selected (the models are placed in the real world). With depthMask, only
   // their depth is written, at the depth the UI is drawn at, for the UI to be drawn around them (see Renderer::RenderVRControllerMask).
   // Returns whether a model was drawn.
   bool Render(Renderer* renderer, const VRDevice& vrDevice, bool depthMask = false);

private:
   struct Primitive
   {
      std::shared_ptr<MeshBuffer> mesh;
      int texture = -1; // Index in Model::textures
      Material material;
      float alphaTest = -1.f; // glTF alpha mask cutoff, or -1 without
      bool transparent = false; // glTF alpha blend
      bool sticker = false; // Front sticker of the controller, which shows our logo (see Load)
   };
   struct Node
   {
      int parent = -1; // Nodes are sorted parents first
      Matrix3D local; // glTF node transform (row vectors)
      vec3 scale { 1.f, 1.f, 1.f }; // Kept when the runtime gives the pose of the node
      int animatable = -1; // Index in the node states of the runtime, or -1 if not animated
      vector<int> primitives;
   };
   struct Model
   {
      uint64_t id = 0;
      vector<Node> nodes;
      vector<Primitive> primitives;
      vector<std::shared_ptr<BaseTexture>> textures; // Shared with the read asset (ReadModel)
      std::shared_ptr<BaseTexture> sticker; // Our logo, for the front sticker of the controllers
   };
   // An asset as read, without anything of a render device: kept for the whole process
   struct ReadPrimitive
   {
      string name;
      vector<Vertex3D_NoTex2> vertices;
      vector<unsigned int> indices;
      int texture = -1;
      Material material;
      float alphaTest = -1.f;
      bool transparent = false;
      bool sticker = false;
   };
   struct ReadModel
   {
      uint64_t id = 0;
      vector<Node> nodes;
      vector<ReadPrimitive> primitives;
      vector<std::shared_ptr<BaseTexture>> textures;
      std::shared_ptr<BaseTexture> sticker;
   };
   enum class ReadState { Reading, Read, Failed };
   // The asset of a controller as read: started on a thread of its own the first time it is asked for, then given from the process cache
   static ReadState GetReadModel(uint64_t id, const std::shared_ptr<const vector<uint8_t>>& glb, const vector<string>& animatableNodes, std::shared_ptr<const ReadModel>& out);
   static std::shared_ptr<const ReadModel> Read(uint64_t id, std::shared_ptr<const vector<uint8_t>> glb, vector<string> animatableNodes);
   std::unique_ptr<Model> Build(const ReadModel& read) const; // Mesh buffers on this render device
   void Release(Model& model) const;

   RenderDevice* const m_rd;
   vector<std::unique_ptr<Model>> m_models;
   vector<uint64_t> m_failedIds; // Assets that could not be loaded, not tried again
   vector<Matrix3D> m_nodeWorld; // Scratch buffers for Render
   vector<bool> m_nodeVisible;
   double m_nextLogTime = 0.;
};

#endif
