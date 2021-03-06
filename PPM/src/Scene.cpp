#include "Scene.h"
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include "Bounding_volume_hierarchy.h"
#include "Mesh.h"
#include "Point_light.h"
#include "Sphere.h"
#include "tinyxml2.h"
//#define GAUSSIAN_FILTER
#define ALPHA 0.7f

void Scene::sample_hemisphere(const Vector3& w, Vector3& d, float& p,
                              bool is_uniform_sampling) {
  thread_local static std::random_device rd;
  thread_local static std::mt19937 generator(rd());
  std::uniform_real_distribution<float> uniform_dist(0.0f, 1.0f);
  float epsilon_1 = uniform_dist(generator);
  float epsilon_2 = uniform_dist(generator);

  float phi;
  float theta;
  const Vector3 u = ((w.x != 0.0f || w.y != 0.0f) ? Vector3(-w.y, w.x, 0.0f)
                                                  : Vector3(0.0f, 1.0f, 0.0f))
                        .normalize();
  const Vector3 v = w.cross(u);
  if (is_uniform_sampling) {
    phi = 2 * M_PI * epsilon_1;
    theta = std::acos(epsilon_2);
  } else {
    phi = 2 * M_PI * epsilon_1;
    theta = std::asin(std::sqrt(epsilon_2));
  }
  d = (w * std::cos(theta) + v * std::sin(theta) * std::cos(phi) +
       u * std::sin(theta) * std::sin(phi))
          .normalize();
  if (is_uniform_sampling) {
    p = 1.0f / (2 * M_PI);
  } else {
    p = std::max(0.0f, w.dot(d)) / M_PI;
  }
}

void Scene::reset_hash_grid() {
  hash_grid.clear();
  for (int i = 0; i < hit_points.size(); i++) {
    delete hit_points[i];
  }
  hit_points.clear();
}
void Scene::build_hash_grid(const int width, const int height) {
  hit_point_bbox = Bounding_box();
  for (int i = 0; i < hit_points.size(); i++) {
    Hit_point* hit_point = hit_points[i];
    hit_point_bbox.fit(hit_point->position);
  }
  Vector3 bbox_size = hit_point_bbox.delta;
  float initial_radius = ((bbox_size.x + bbox_size.y + bbox_size.z) / 3.0f) /
                         ((width + height) / 2.0f) * 2.0f * 4.0f;
  hit_point_bbox = Bounding_box();
  num_hash = hit_points.size();
  for (int i = 0; i < hit_points.size(); i++) {
    Hit_point* hit_point = hit_points[i];
    hit_point->radius_squared = initial_radius * initial_radius;
    hit_point->n = 0;
    hit_point->flux = Vector3(0.0f);
    hit_point_bbox.fit(hit_point->position - initial_radius);
    hit_point_bbox.fit(hit_point->position + initial_radius);
  }
  hash_scale = 1.0 / (initial_radius * 2.0);

  hash_grid.resize(num_hash);
  for (int i = 0; i < hit_points.size(); i++) {
    Hit_point* hit_point = hit_points[i];
    Vector3 BMin =
        ((hit_point->position - initial_radius) - hit_point_bbox.min_corner) *
        hash_scale;
    Vector3 BMax =
        ((hit_point->position + initial_radius) - hit_point_bbox.min_corner) *
        hash_scale;
    for (int iz = std::abs(int(BMin.z)); iz <= std::abs(int(BMax.z)); iz++) {
      for (int iy = std::abs(int(BMin.y)); iy <= std::abs(int(BMax.y)); iy++) {
        for (int ix = std::abs(int(BMin.x)); ix <= std::abs(int(BMax.x));
             ix++) {
          int hv = hash(ix, iy, iz);
          hash_grid[hv].push_back(hit_point);
        }
      }
    }
  }
}

void Scene::trace_n_photons(int n, int iteration_count) {
  // For now, only one light source
  const Light* light = lights[0];
  Ray photon_ray(0.0f, 0.0f);
  Vector3 flux;
  for (int i = 0; i < n * iteration_count; i++) {
    light->generate_photon(photon_ray, flux);
    photon_trace(photon_ray, 0, flux);
  }
}

void Scene::photon_trace(const Ray& ray, int depth, const Vector3& flux) {
  thread_local static std::random_device rd;
  thread_local static std::mt19937 generator(rd());
  std::uniform_real_distribution<float> uniform_dist(0.0f, 1.0f);
  Intersection intersection;
  depth++;

  if ((depth >= max_recursion_depth) ||
      !bvh->intersect(ray, intersection, true)) {
    return;
  }
  const Shape* shape = intersection.shape;
  Vector3 intersection_point = ray.point_at(intersection.t);
  const Vector3& normal = intersection.normal;
  Vector3 nl = normal.dot(ray.d) < 0 ? normal : normal * -1;
  const Material& material = materials[shape->get_material_id()];
  if (material.material_type == mt_diffuse) {
    // Use Quasi-Monte Carlo to sample the next direction

    Vector3 hh = (intersection_point - hit_point_bbox.min_corner) * hash_scale;
    int ix = abs(int(hh.x));
    int iy = abs(int(hh.y));
    int iz = abs(int(hh.z));
    {
      std::vector<Hit_point*>& hpoints = hash_grid[hash(ix, iy, iz)];
      for (int i = 0; i < hpoints.size(); i++) {
        Hit_point* hit_point = hpoints[i];

        Vector3 v = hit_point->position - intersection_point;
        hit_point->mutex.lock();
        if ((hit_point->normal.dot(normal) > 1e-3f) &&
            (v.dot(v) <= hit_point->radius_squared)) {
          // Unlike N in the paper, hit_point->n stores "N / ALPHA" to make it
          // an integer value
          float radius_reduction =
              (hit_point->n * ALPHA + ALPHA) / (hit_point->n * ALPHA + 1.0);
          hit_point->radius_squared =
              hit_point->radius_squared * radius_reduction;
          hit_point->n++;
          Vector3 color(0.0f);
          Vector3 w_i = -ray.d.normalize();
          if (hit_point->material.brdf_id == -1) {
            // all things except w_i should be used from hit_point
            float cos_theta_i = hit_point->normal.dot(w_i);
            if (cos_theta_i > 1.0f || cos_theta_i <= 0.0f) {
              color = 0.0f;
            } else {
              float specular_cos_theta = std::max(
                  0.0f,
                  hit_point->normal.dot((hit_point->w_o + w_i).normalize()));
              color = (hit_point->material.diffuse +
                       hit_point->material.specular *
                           std::pow(specular_cos_theta,
                                    hit_point->material.phong_exponent) /
                           cos_theta_i) *
                      hit_point->attenuation;
            }

          } else {
            // parse brdfs, use brdfs with hit_point's diffuse, specular
          }
          hit_point->flux = (hit_point->flux + color * flux) * radius_reduction;
        }
        hit_point->mutex.unlock();
      }
    }
    float probability;
    Vector3 d;
    sample_hemisphere(normal, d, probability);
    Vector3 base_color(0.0f);
    Vector3 w_i = -ray.d.normalize();
    float cos_theta_i = std::max(0.0f, normal.dot(w_i));
    const Vector3& w_o = d;

    if (material.brdf_id == -1) {
      if (cos_theta_i > 1.0f || cos_theta_i <= 0.0f) {
        base_color = 0.0f;
      } else {
        float specular_cos_theta =
            std::max(0.0f, nl.dot((w_o + w_i).normalize()));
        base_color = (material.diffuse +
                      (material.specular *
                       std::pow(specular_cos_theta, material.phong_exponent) /
                       cos_theta_i));
      }
    } else {
      // parse brdfs, use brdfs with intersection's diffuse, specular
    }
    float cos_theta_o = std::max(0.0f, normal.dot(w_o));
    base_color *= cos_theta_o;
    if (uniform_dist(generator) < probability) {
      photon_trace(Ray(intersection_point + (d * shadow_ray_epsilon), d), depth,
                   (base_color * flux) / probability);
    }
  } else if (material.material_type == mt_mirror) {
    const Vector3 w_o = (ray.o - intersection_point).normalize();
    const Vector3 w_r = ((2.0f * normal.dot(w_o) * normal) - w_o).normalize();
    Ray mirror_ray(intersection_point + (w_r * shadow_ray_epsilon), w_r);
    photon_trace(mirror_ray, depth, material.mirror * flux);

  } else if (material.material_type == mt_refractive) {
    const Vector3 nl = normal.dot(ray.d) < 0.0f ? normal : normal * -1;
    const Vector3 w_o = (ray.o - intersection_point).normalize();
    const Vector3 w_r = ((2.0f * normal.dot(w_o) * normal) - w_o).normalize();
    Ray reflection_ray(intersection_point + (w_r * shadow_ray_epsilon), w_r);
    bool into = (normal.dot(nl) > 0.0f);
    float air_index = 1.0f;
    // if (material.refraction_index == 2.0f) {
    //  air_index = 1.33;
    //}
    float nnt = into ? air_index / material.refraction_index
                     : material.refraction_index / air_index;
    float ddn = ray.d.dot(nl);
    float cos2t = 1 - nnt * nnt * (1 - ddn * ddn);
    if (cos2t < 0.0f) {
      photon_trace(reflection_ray, depth, flux);
      return;
    }
    Vector3 refraction_direction =
        (ray.d * nnt -
         normal * ((into ? 1 : -1) * (ddn * nnt + std::sqrt(cos2t))))
            .normalize();
    float a = material.refraction_index - air_index;
    float b = material.refraction_index + air_index;
    float R0 = a * a / (b * b);

    float cosinealpha = into ? -ddn : refraction_direction.dot(normal);
    float c = 1 - cosinealpha;
    float fresnel = R0 + (1 - R0) * c * c * c * c * c;
    float P = fresnel;
    Ray refraction_ray(
        intersection_point + (refraction_direction * shadow_ray_epsilon),
        refraction_direction);
    if (into) {
      if (uniform_dist(generator) < P) {
        photon_trace(reflection_ray, depth, flux);
      } else {
        photon_trace(refraction_ray, depth, flux);
      }
    } else {
      photon_trace(refraction_ray, depth, flux);
    }
  }
}
void Scene::eye_trace_lines(int index, int starting_row, int height_increase) {
  const Camera& camera = cameras[index];
  const Image_plane& image_plane = camera.get_image_plane();
  const int width = image_plane.width;
  const int height = image_plane.height;
  int number_of_samples = camera.get_number_of_samples();
  if (number_of_samples == 1) {
    for (int j = starting_row; j < height; j += height_increase) {
      for (int i = 0; i < width; i++) {
        Ray primary_ray = camera.calculate_ray_at(i + 0.5f, j + 0.5f);
        eye_trace(primary_ray, 0, 1.0f, j * width + i, 1.0f);
      }
    }
  } else {
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_real_distribution<float> ms_distribution(0.0f, 1.0f);
    // Maximum 4 samples for one pixel
    number_of_samples = std::min(2, number_of_samples);
    for (int j = starting_row; j < height; j += height_increase) {
      for (int i = 0; i < width; i++) {
        for (int x = 0; x < number_of_samples; x++) {
          for (int y = 0; y < number_of_samples; y++) {
            float epsilon_x = ms_distribution(generator);
            float epsilon_y = ms_distribution(generator);
            float sample_x = (x + epsilon_x) / number_of_samples;
            float sample_y = (y + epsilon_y) / number_of_samples;
            Ray primary_ray =
                camera.calculate_ray_at(i + sample_x, j + sample_y);
            eye_trace(primary_ray, 0, 1.0f, j * width + i, 1.0f);
          }
        }
      }
    }
  }
}
void Scene::eye_trace(const Ray& ray, int depth, const Vector3& attenuation,
                      unsigned int pixel_index, float pixel_weight) {
  Intersection intersection;
  if (!bvh->intersect(ray, intersection, true)) {
    return;
  }
  const Shape* shape = intersection.shape;
  Vector3 intersection_point = ray.point_at(intersection.t);
  const Vector3& normal = intersection.normal;
  const Material& material = materials[shape->get_material_id()];
  if (material.material_type == mt_diffuse) {
    Hit_point* hit_point = new Hit_point();
    hit_point->material = material;
    hit_point->attenuation = attenuation;
    hit_point->w_o = (ray.o - intersection_point).normalize();
    hit_point->normal = normal;
    hit_point->position = intersection_point;
    hit_point->pixel = pixel_index;
    hit_point->pixel_weight = pixel_weight;
    add_hit_point(hit_point);
  } else if (depth >= max_recursion_depth) {
    return;
  } else if (material.material_type == mt_mirror) {
    const Vector3 w_o = (ray.o - intersection_point).normalize();
    const Vector3 w_r = ((2.0f * normal.dot(w_o) * normal) - w_o).normalize();
    Ray mirror_ray(intersection_point + (w_r * shadow_ray_epsilon), w_r);
    eye_trace(mirror_ray, depth + 1, material.mirror * attenuation, pixel_index,
              pixel_weight);
  } else if (material.material_type == mt_refractive) {
    // TODO: this calculation does not calculate attenuation with
    // exp(log(transparency) * hit_data_t).
    // uses only transparency while refracting ray, maybe we can convert it to
    // our formula, AKCAY?
    const Vector3 nl = normal.dot(ray.d) < 0.0f ? normal : normal * -1;
    const Vector3 w_o = (ray.o - intersection_point).normalize();
    const Vector3 w_r = ((2.0f * normal.dot(w_o) * normal) - w_o).normalize();
    Ray reflection_ray(intersection_point + (w_r * shadow_ray_epsilon), w_r);
    bool into = (normal.dot(nl) > 0.0f);
    float air_index = 1.0f;
    // if (material.refraction_index == 2.0f) {
    //  air_index = 1.33;
    //}
    float nnt = into ? air_index / material.refraction_index
                     : material.refraction_index / air_index;
    float ddn = ray.d.dot(nl);
    float cos2t = 1 - nnt * nnt * (1 - ddn * ddn);
    if (cos2t < 0.0f) {
      eye_trace(reflection_ray, depth + 1, material.transparency * attenuation,
                pixel_index, pixel_weight);
      return;
    }
    Vector3 refraction_direction =
        (ray.d * nnt -
         normal * ((into ? 1 : -1) * (ddn * nnt + std::sqrt(cos2t))))
            .normalize();
    float a = material.refraction_index - air_index;
    float b = material.refraction_index + air_index;
    float R0 = a * a / (b * b);

    float cosinealpha = into ? -ddn : refraction_direction.dot(normal);
    float c = 1 - cosinealpha;
    float fresnel = R0 + (1 - R0) * c * c * c * c * c;
    Ray refraction_ray(
        intersection_point + (refraction_direction * shadow_ray_epsilon),
        refraction_direction);
    Vector3 attenuated_color = material.transparency * attenuation;
    if (into) {
      eye_trace(reflection_ray, depth + 1, fresnel * attenuation, pixel_index,
                pixel_weight);
      eye_trace(refraction_ray, depth + 1, (1.0f - fresnel) * attenuated_color,
                pixel_index, pixel_weight);
    } else {
      eye_trace(refraction_ray, depth + 1, attenuated_color, pixel_index,
                pixel_weight);
    }
  }
}
void Scene::density_estimation(Pixel* pixels, int total_num_of_photons) {
  for (int i = 0; i < hit_points.size(); i++) {
    const Hit_point* hit_point = hit_points[i];
    int pixel_index = hit_point->pixel;
    pixels[pixel_index].add_color(
        hit_point->flux *
            (1.0f / (M_PI * hit_point->radius_squared * total_num_of_photons)),
        hit_point->pixel_weight);
  }
}

Scene::Scene(const std::string& file_name) {
  tinyxml2::XMLDocument file;
  std::stringstream stream;
  auto res = file.LoadFile(file_name.c_str());
  if (res) {
    throw std::runtime_error("Error: The xml file cannot be loaded.");
  }
  auto root = file.FirstChild();
  if (!root) {
    throw std::runtime_error("Error: Root is not found.");
  }

  // Get ShadowRayEpsilon
  auto element = root->FirstChildElement("ShadowRayEpsilon");
  if (element) {
    stream << element->GetText() << std::endl;
  } else {
    stream << "0.001" << std::endl;
  }
  stream >> shadow_ray_epsilon;
  std::cout << "ShadowRayEpsilon is parsed" << std::endl;
  //

  // Get PhotonCountPerIteration
  element = root->FirstChildElement("PhotonCountPerIteration");
  if (element) {
    stream << element->GetText() << std::endl;
  } else {
    stream << "8000" << std::endl;
  }
  stream >> photon_count_per_iteration;
  std::cout << "PhotonCountPerIteration is parsed" << std::endl;
  //

  // Get NumberOfIterations
  element = root->FirstChildElement("NumberOfIterations");
  if (element) {
    stream << element->GetText() << std::endl;
  } else {
    stream << "1000" << std::endl;
  }
  stream >> number_of_iterations;
  std::cout << "NumberOfIterations is parsed" << std::endl;
  //

  // Get MaxRecursionDepth
  element = root->FirstChildElement("MaxRecursionDepth");
  if (element) {
    stream << element->GetText() << std::endl;
  } else {
    stream << "20" << std::endl;
  }
  stream >> max_recursion_depth;
  if (max_recursion_depth > 20) {
    max_recursion_depth = 20;
    std::cout << "MaxRecursionDepth cannot be greater than 20" << std::endl;
  }
  std::cout << "MaxRecursionDepth is parsed" << std::endl;
  //

  // Get Cameras
  element = root->FirstChildElement("Cameras");
  if (element) {
    Camera::load_cameras_from_xml(element, cameras);
  }
  // Cameras End

  // Get Materials
  element = root->FirstChildElement("Materials");
  if (element) {
    Material::load_materials_from_xml(element, materials);
  }
  // Materials End

  // Get Transformations
  element = root->FirstChildElement("Transformations");
  if (element) {
    Translation::load_translation_transformations_from_xml(
        element, translation_transformations);
    Scaling::load_scaling_transformations_from_xml(element,
                                                   scaling_transformations);
    Rotation::load_rotation_transformations_from_xml(element,
                                                     rotation_transformations);
  }
  // Transformations End

  // Get VertexData
  element = root->FirstChildElement("VertexData");
  if (element) {
    const char* binary_file = element->Attribute("binaryFile");
    if (binary_file) {
      // parse_binary_vertexdata(std::string(binary_file));
    } else {
      stream << element->GetText() << std::endl;
      Vector3 vertex;
      while (!(stream >> vertex.x).eof()) {
        stream >> vertex.y >> vertex.z;
        vertex_data.push_back(Vertex(vertex));
      }
    }
  }
  stream.clear();
  std::cout << "VertexData is parsed" << std::endl;
  // VertexData End
  // Get Lights
  element = root->FirstChildElement("Lights");
  if (element) {
    Point_light::load_point_lights_from_xml(element, lights);
  }
  //
  // Get Objects
  std::vector<Shape*> objects;
  element = root->FirstChildElement("Objects");
  if (element) {
    Sphere::load_spheres_from_xml(
        this, element, objects, vertex_data, scaling_transformations,
        translation_transformations, rotation_transformations);
    Mesh::load_meshes_from_xml(
        this, element, meshes, vertex_data, scaling_transformations,
        translation_transformations, rotation_transformations);
    Mesh_instance::create_mesh_instances_for_meshes(meshes, objects, materials);
    Mesh_instance::load_mesh_instances_from_xml(
        element, meshes, objects, materials, scaling_transformations,
        translation_transformations, rotation_transformations);
  }
  for (Vertex& vertex : vertex_data) {
    vertex.finalize_normal();
  }
  bvh = BVH::create_bvh(objects);
  //
}

Scene::~Scene() {}
