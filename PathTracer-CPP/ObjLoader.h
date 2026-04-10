#pragma once
#include<vector>
#include<string>
#include<memory>
#include<filesystem>
#include "Triangle.h"
#include "Material.h"
#include "Hittable_List.h"

#include "external/tiny_obj_loader.h"

class ObjLoader {
public:
    static std::shared_ptr<Hittable_List> load(
        const std::filesystem::path& filepath, std::shared_ptr<Material> default_mat)
    {
        auto mesh_list = std::make_shared<Hittable_List>();

        tinyobj::ObjReaderConfig reader_config;
        // If there has a mtl file
        reader_config.mtl_search_path = "./";

        tinyobj::ObjReader reader;
        if (!reader.ParseFromFile(filepath.string(), reader_config))
        {
            if (!reader.Error().empty())
            {
                std::cerr << "TinyObjReader Error: " << reader.Error();
            }
            return nullptr;
        }

        if (!reader.Warning().empty())
        {
            std::cout << "TinyObjReader Warning: " << reader.Warning();
        }

        auto& attrib = reader.GetAttrib();   // Vertex, Normal, UV
        auto& shapes = reader.GetShapes();   // Face

        for (size_t s = 0; s < shapes.size(); s++)
        {
            size_t index_offset = 0;
            for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
            {
                size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

                if (fv == 3)
                {
                    Point3 vertices[3];
                    Vector3 normals[3];
                    bool face_has_normals = true;

                    for (size_t v = 0; v < 3; v++)
                    {
                        tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

                        // Abstruct Vertex's Corrinate
                        tinyobj::real_t vx = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
                        tinyobj::real_t vy = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
                        tinyobj::real_t vz = attrib.vertices[3 * size_t(idx.vertex_index) + 2];
                        vertices[v] = Point3(vx, vy, vz);

                        if (idx.normal_index >= 0)
                        {
                            tinyobj::real_t nx = attrib.normals[3 * size_t(idx.normal_index) + 0];
                            tinyobj::real_t ny = attrib.normals[3 * size_t(idx.normal_index) + 1];
                            tinyobj::real_t nz = attrib.normals[3 * size_t(idx.normal_index) + 2];
                            normals[v] = Vector3(nx, ny, nz);
                        }
                        else
                        {
                            // As long as the normals of one vertex are incomplete
                            // then it's demoted to flat shading directly
                            face_has_normals = false;
                        }
                    }

                    // Depending on whether normals have been extracted, 
                    // a different constructor is invoked.
                    if (face_has_normals)
                    {
                        mesh_list->add(std::make_shared<Triangle>(
                            vertices[0], vertices[1], vertices[2],
                            normals[0], normals[1], normals[2], default_mat));
                    }
                    else
                    {
                        mesh_list->add(std::make_shared<Triangle>(
                            vertices[0], vertices[1], vertices[2], default_mat));
                    }
                }
                index_offset += fv;
            }
        }

        return mesh_list;
    }
};