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
        const std::filesystem::path obj_directory = filepath.has_parent_path() ? filepath.parent_path() : std::filesystem::path(".");

        tinyobj::ObjReaderConfig reader_config;
        reader_config.mtl_search_path = obj_directory.string();

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
        auto& materials = reader.GetMaterials();

        std::vector<std::shared_ptr<Material>> material_cache(materials.size());

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
					TexCoord2 texcoords[3];
                    bool face_has_normals = true;
                    bool face_has_texcoords = true;

                    for (size_t v = 0; v < 3; v++)
                    {
                        tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

                        // Abstruct Vertex's Corrinate
                        tinyobj::real_t vx = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
                        tinyobj::real_t vy = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
                        tinyobj::real_t vz = attrib.vertices[3 * size_t(idx.vertex_index) + 2];
                        vertices[v] = Point3(vx, vy, vz);

                        // Abstruct Texture's Corrinate
                        if (idx.texcoord_index >= 0)
                        {
                            tinyobj::real_t tx = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
                            tinyobj::real_t ty = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
                            texcoords[v] = TexCoord2(tx, ty);
                        }
                        else
                        {
                            texcoords[v] = TexCoord2();
                            face_has_texcoords = false;
                        }

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

                    std::shared_ptr<Material> face_material = default_mat;
                    if (f < shapes[s].mesh.material_ids.size())
                    {
                        int material_id = shapes[s].mesh.material_ids[f];
                        if (material_id >= 0 && static_cast<size_t>(material_id) < materials.size())
                        {
                            face_material = get_or_create_material(materials[static_cast<size_t>(material_id)], material_cache[static_cast<size_t>(material_id)], obj_directory, default_mat);
                        }
                    }

                    // Depending on whether normals have been extracted, 
                    // a different constructor is invoked.
                    if (face_has_normals)
                    {
                        if (face_has_texcoords)
                        {
                            mesh_list->add(std::make_shared<Triangle>(
                                vertices[0], vertices[1], vertices[2],
                                normals[0], normals[1], normals[2],
                                texcoords[0], texcoords[1], texcoords[2],
                                face_material));
                        }
                        else
                        {
                            mesh_list->add(std::make_shared<Triangle>(
                                vertices[0], vertices[1], vertices[2],
                                normals[0], normals[1], normals[2], face_material));
                        }
                    }
                    else
                    {
                        if (face_has_texcoords)
                        {
                            mesh_list->add(std::make_shared<Triangle>(
                                vertices[0], vertices[1], vertices[2],
                                texcoords[0], texcoords[1], texcoords[2],
                                face_material));
                        }
                        else
                        {
                            mesh_list->add(std::make_shared<Triangle>(
                                vertices[0], vertices[1], vertices[2], face_material));
                        }
                    }
                }
                index_offset += fv;
            }
        }

        return mesh_list;
    }

private:
    static std::shared_ptr<Material> get_or_create_material(
        const tinyobj::material_t& source_material,
        std::shared_ptr<Material>& cached_material,
        const std::filesystem::path& obj_directory,
        const std::shared_ptr<Material>& default_mat)
    {
        if (cached_material)
            return cached_material;

        if (!source_material.diffuse_texname.empty())
        {
            const std::filesystem::path texture_path = obj_directory / source_material.diffuse_texname;
            cached_material = std::make_shared<Lambertian>(
                std::make_shared<Image_Texture>(texture_path.string()));
            return cached_material;
        }

        const Color diffuse_color(
            source_material.diffuse[0],
            source_material.diffuse[1],
            source_material.diffuse[2]);

        if (diffuse_color.length_squared() > 0.0)
        {
            cached_material = std::make_shared<Lambertian>(diffuse_color);
            return cached_material;
        }

        cached_material = default_mat;
        return cached_material;
    }
};
