// license:GPLv3+

// Steam Frame Edition: the in-game UI is displayed on a panel standing in the room, pointed at with the aim ray of a controller.
// VRDevice members, defined here to keep the upstream file close to upstream's.

#include "core/stdafx.h"
#include "renderer/VRDevice.h"

#if defined(ENABLE_XR)

#ifdef _far
   #undef _far
#endif
#ifdef _near
   #undef _near
#endif
#include "bx/math.h"
#include "math/matrix.h"

// Same as the helper of VRDevice.cpp
inline static void XrPosef_ToMatrix3D(Matrix3D* result, const XrPosef* pose)
{
   const bx::Quaternion orientation(pose->orientation.x, pose->orientation.y, pose->orientation.z, pose->orientation.w);
   const bx::Quaternion invertOrientation = bx::conjugate(orientation);
   bx::mtxFromQuaternion(&result->m[0][0], invertOrientation);
   result->Transpose();
   const bx::Quaternion position(pose->position.x, pose->position.y, pose->position.z, 0.f);
   bx::Quaternion invertPosition = bx::conjugate(position);
   invertPosition = bx::mul(invertPosition, orientation);
   invertPosition = bx::mul(invertOrientation, invertPosition);
   result->m[3][0] += invertPosition.x;
   result->m[3][1] += invertPosition.y;
   result->m[3][2] += invertPosition.z;
}

// The UI is displayed on a vertical panel standing in the room (the reference space, in meters), placed in front of the head, facing the
// table's forward direction, when it is first displayed. Pointing at it is intersecting the aim ray of a controller with that panel.
void VRDevice::UpdateUIPanel(const std::vector<XrView>& views, XrTime time)
{
   m_uiPointerValid = false;
   if (views.size() < 2)
      return;

   const auto rotate = [](const XrQuaternionf& q, const vec3& v)
   {
      const vec3 u(q.x, q.y, q.z);
      const vec3 t = CrossProduct(u, v) * 2.f;
      return v + t * q.w + CrossProduct(u, t);
   };

   const vec3 head = (vec3(views[0].pose.position.x, views[0].pose.position.y, views[0].pose.position.z) + vec3(views[1].pose.position.x, views[1].pose.position.y, views[1].pose.position.z)) * 0.5f;
   m_uiHeadPos = head;
   if (!m_uiPanelPlaced)
   {
      // The panel covers the whole UI display area, most of it being transparent. The UI windows are laid out for the full field of view
      // of the headset: at 1m, 2.8m wide gives about 109 degrees (98 at the default distance of 1.2m).
      // The player chooses the distance (Standalone.VRMenuDistance), keeping the size, so a closer window looks bigger.
      constexpr float width = 2.8f, belowEyes = 0.1f; // meters
      const float distance = clamp(g_settingsService.GetAppSettings().GetStandalone_VRMenuDistance(), 0.3f, 3.f);
      const float height = width * static_cast<float>(m_eyeHeight) / static_cast<float>(m_eyeWidth);
      // Upright panel in the forward direction of the table (the way the player faces it at the lockbar), wherever the head looks, so that
      // the player turns to the table, or moves, to read the menu. The scene orientation is the yaw of that direction (see the table
      // centering in RenderFrame: looking along -Z gives 0, turned left towards -X gives +90 degrees).
      const float orientation = ANGTORAD(m_orientation);
      const vec3 forward(-sinf(orientation), 0.f, -cosf(orientation));
      const vec3 up(0.f, 1.f, 0.f);
      const vec3 right = CrossProduct(forward, up);
      const vec3 center = head + forward * distance - up * belowEyes;
      m_uiPanelRight = right * width;
      m_uiPanelDown = up * -height;
      m_uiPanelOrigin = center - m_uiPanelRight * 0.5f - m_uiPanelDown * 0.5f;
      m_uiPanelPlaced = true;
   }

   // Depth is not used (the shader outputs a constant depth, the UI being drawn over the scene), so near and far planes do not matter
   for (int eye = 0; eye < 2; eye++)
   {
      Matrix3D view, proj;
      XrPosef_ToMatrix3D(&view, &views[eye].pose);
      proj.SetPerspectiveFovRH(views[eye].fov.angleLeft, views[eye].fov.angleRight, views[eye].fov.angleDown, views[eye].fov.angleUp, 0.05f, 100.f);
      m_uiPanelViewProj[eye] = view * proj;
   }

   if (m_xrInputHandler == nullptr)
      return;

   // Right hand first, left hand if the right one is not tracked or does not point at the UI
   const vec3 normal = CrossProduct(m_uiPanelRight, m_uiPanelDown);
   struct Hand { XrSpace space; const char* trigger; const char* thumbstickY; };
   for (const auto& [space, trigger, thumbstickY] : { Hand { m_rightAimSpace, "/user/hand/right/input/trigger/value", "/user/hand/right/input/thumbstick/y" },
                                                      Hand { m_leftAimSpace, "/user/hand/left/input/trigger/value", "/user/hand/left/input/thumbstick/y" } })
   {
      XrSpaceLocation location { XR_TYPE_SPACE_LOCATION };
      if (space == XR_NULL_HANDLE || xrLocateSpace(space, m_referenceSpace, time, &location) != XR_SUCCESS)
         continue;
      if ((location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0 || (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0)
         continue;
      const vec3 origin(location.pose.position.x, location.pose.position.y, location.pose.position.z);
      const vec3 direction = rotate(location.pose.orientation, vec3(0.f, 0.f, -1.f));
      const float denom = direction.Dot(normal);
      if (fabsf(denom) < 1e-6f)
         continue;
      const float t = (m_uiPanelOrigin - origin).Dot(normal) / denom;
      if (t <= 0.f)
         continue;
      const vec3 onPanel = origin + direction * t - m_uiPanelOrigin;
      const float x = onPanel.Dot(m_uiPanelRight) / m_uiPanelRight.LengthSquared();
      const float y = onPanel.Dot(m_uiPanelDown) / m_uiPanelDown.LengthSquared();
      if (x < 0.f || x > 1.f || y < 0.f || y > 1.f)
         continue;

      m_uiPointerValid = true;
      m_uiPointerX = x;
      m_uiPointerY = y;
      m_uiPointerRayStart = origin;
      m_uiPointerRayEnd = origin + direction * t;
      m_uiPointerScroll = m_xrInputHandler->GetFloatState(thumbstickY);
      // Hysteresis, as this is an analog trigger
      const float triggerValue = m_xrInputHandler->GetFloatState(trigger);
      m_uiPointerPressed = m_uiPointerPressed ? (triggerValue > 0.4f) : (triggerValue > 0.7f);
      return;
   }
   m_uiPointerPressed = false;
   m_uiPointerScroll = 0.f;
}

bool VRDevice::GetUIPointerRayTransforms(Matrix3D (&quadToClip)[2]) const
{
   if (!m_uiPointerValid)
      return false;
   // A flat ribbon from the controller to the pointed position, turned towards the head so that it is seen from its wide side
   constexpr float thickness = 0.004f; // meters
   const vec3 along = m_uiPointerRayEnd - m_uiPointerRayStart;
   vec3 across = CrossProduct(along, m_uiHeadPos - m_uiPointerRayStart);
   if (across.LengthSquared() < 1e-10f)
      return false;
   across.Normalize();
   across = across * thickness;
   const vec3 start = m_uiPointerRayStart - across * 0.5f;
   const Matrix3D quadToWorld(
      along.x, along.y, along.z, 0.f,
      across.x, across.y, across.z, 0.f,
      0.f, 0.f, 1.f, 0.f,
      start.x, start.y, start.z, 1.f);
   for (int eye = 0; eye < 2; eye++)
      quadToClip[eye] = quadToWorld * m_uiPanelViewProj[eye];
   return true;
}

bool VRDevice::GetUIPanelTransforms(float width, float height, Matrix3D (&pixelToClip)[2]) const
{
   if (!m_uiPanelPlaced)
      return false;
   // UI pixel to reference space: the panel origin, then its full width and height spread over the UI display size
   const vec3 dx = m_uiPanelRight * (1.f / width), dy = m_uiPanelDown * (1.f / height);
   const Matrix3D pixelToPanel(
      dx.x, dx.y, dx.z, 0.f,
      dy.x, dy.y, dy.z, 0.f,
      0.f, 0.f, 1.f, 0.f,
      m_uiPanelOrigin.x, m_uiPanelOrigin.y, m_uiPanelOrigin.z, 1.f);
   for (int eye = 0; eye < 2; eye++)
      pixelToClip[eye] = pixelToPanel * m_uiPanelViewProj[eye];
   return true;
}

#endif
