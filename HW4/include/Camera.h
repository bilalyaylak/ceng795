#pragma once
#ifndef CAMERA_H_
#define CAMERA_H_
#include <string>
#include "Image_plane.h"
#include "Ray.h"
#include "Vector3.h"
class Camera {
 public:
  Camera(const Vector3& up, const Vector3& gaze, const Vector3& position,
         int number_of_samples, const std::string& filename, float left,
         float right, float bottom, float top, float distance, int image_width,
         int image_height, float focus_distance, float aperture_size)
      : e(position),
        number_of_samples_(number_of_samples),
        aperture_size_(aperture_size),
        filename_(filename),
        image_plane_(left, right, bottom, top, distance, image_width,
                     image_height) {
    w = -(gaze.normalize());
    u = up.normalize().cross(w).normalize();
    v = w.cross(u).normalize();
    if (aperture_size_ != 0.0f) {
      float ratio = focus_distance / distance;
      image_plane_ =
          Image_plane(ratio * left, ratio * right, ratio * bottom, ratio * top,
                      focus_distance, image_width, image_height);
    }
    top_left_corner = e - w * image_plane_.distance + image_plane_.left * u +
                      image_plane_.top * v;
    s_u_constant =
        ((image_plane_.right - image_plane_.left) / image_plane_.width) * u;
    s_v_constant =
        ((image_plane_.top - image_plane_.bottom) / image_plane_.height) * v;
  }
  Ray calculate_ray_at(float x, float y, float time = 0.0f) const {
    // For now, origin is top_left, right handed camera
    const Vector3 s =
        top_left_corner + x * s_u_constant - y * s_v_constant;
    Ray ray(e, (s - e).normalize(), r_primary, time);
    ray.bg_u = x/image_plane_.width;
    ray.bg_v = y/image_plane_.height;
    return ray;
  }
  Ray calculate_ray_at(float x, float y, float x_offset_ratio,
                       float y_offset_ratio, float time = 0.0f) const {
    // For now, origin is top_left, right handed camera
    Vector3 new_e =
        e + (u * x_offset_ratio + v * y_offset_ratio) * aperture_size_ * 0.5f;
    const Vector3 s =
        top_left_corner + x * s_u_constant - y * s_v_constant;
    Ray ray(new_e, (s - new_e).normalize(), r_primary, time);
    ray.bg_u = x/image_plane_.width;
    ray.bg_v = y/image_plane_.height;
    return ray;
  }
  const Image_plane& get_image_plane() const { return image_plane_; }
  const std::string& get_filename() const { return filename_; }
  int get_number_of_samples() const { return number_of_samples_; }
  float get_aperture_size() const { return aperture_size_; }

 private:
  // implement a matrix frame for both basis and position?
  Vector3 u;  // side vector
  Vector3 v;  // up vector
  Vector3 w;  // forward vector
  Vector3 e;  // position

  Vector3 s_u_constant;
  Vector3 s_v_constant;
  Vector3 top_left_corner;

  int number_of_samples_;
  float aperture_size_;
  const std::string filename_;
  Image_plane image_plane_;
};
#endif
