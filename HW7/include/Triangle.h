#pragma once
#ifndef TRIANGLE_H_
#define TRIANGLE_H_
#include <limits>
#include "Ray.h"
#include "Shape.h"
#include "Transformation.h"
#include "Vector3.h"
class Scene;
class Triangle : public Shape {
 public:
  int index_0, index_1, index_2;
  int offset;
  Vector3 normal;
  int material_id;
  int texture_id;
  Triangle(const Scene* scene, int index_0, int index_1, int index_2,
           int offset, int material_id, int texture_id,
           const Transformation& transformation);
  bool intersect(const Ray& ray, Hit_data& hit_data,
                 bool culling) const override;
  const Bounding_box& get_bounding_box() const override {
    return bounding_box_;
  }
  int get_material_id() const override { return material_id; }
  int get_texture_id() const override { return texture_id; }
  void print_debug(int indentation) const override {
    for (int index = 0; index < indentation; index++) {
      std::cout << "\t";
    }
    std::cout << "Triangle(" << index_0 << "," << index_1 << "," << index_2
              << "), material: " << material_id << std::endl;
  }
  float get_surface_area() const;

 private:
  Bounding_box bounding_box_;
  Transformation transformation_;
  const Scene* scene_;
  bool is_identity_;
  inline float determinant(const Vector3& col1, const Vector3& col2,
                           const Vector3& col3) const {
    return col1.x * (col2.y * col3.z - col3.y * col2.z) +
           col2.x * (col3.y * col1.z - col1.y * col3.z) +
           col3.x * (col1.y * col2.z - col2.y * col1.z);
  }
};
#endif
