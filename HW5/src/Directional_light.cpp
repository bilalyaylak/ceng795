#include "Directional_light.h"

Directional_light::Directional_light(const Vector3& direction,
                                     const Vector3& radiance)
    : direction_(direction), radiance_(radiance) {}

Vector3 Directional_light::direction_and_distance(const Vector3& from_point,
                                                  float& distance) const {
  distance = std::numeric_limits<float>::max();
  return -direction_;
}

Vector3 Directional_light::incoming_radiance(
    const Vector3& from_point_to_light) const {
  return radiance_;
}
