#ifndef LIGHT_H_
#define LIGHT_H_
#include "Vector3.h"
class Light {
 public:
  virtual Vector3 direction_and_distance(const Vector3& from_point,
                                         float& distance) const = 0;

  // Incoming radiance to the point from the light
  virtual Vector3 incoming_radiance(
      const Vector3& from_point_to_light) const = 0;
};
#endif
