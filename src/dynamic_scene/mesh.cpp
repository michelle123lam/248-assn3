#include "mesh.h"
#include "CS248/lodepng.h"

#include <cassert>
#include <sstream>

#include "../static_scene/object.h"
#include "../static_scene/light.h"

using namespace std;
using std::ostringstream;

namespace CS248 {
namespace DynamicScene {

// For use in choose_hovered_subfeature.
static const double low_threshold = .1;
static const double mid_threshold = .2;
static const double high_threshold = 1.0 - low_threshold;

Mesh::Mesh(Collada::PolymeshInfo &polyMesh, const Matrix4x4 &transform, const std::string shader_prefix) {

    for (const Collada::Polygon &p : polyMesh.polygons) {
        polygons.push_back(p.vertex_indices);
    }

    vector<Vector3D> vertices = polyMesh.vertices;  // DELIBERATE COPY
    
    simple_renderable = polyMesh.is_obj_file;
    simple_colors = polyMesh.is_mtl_file;
    if (!simple_renderable)
        return;

    polygons_carbon_copy = polyMesh.polygons;
	position = polyMesh.position;
	rotation = polyMesh.rotation;
	scale = polyMesh.scale;
	do_texture_mapping = false;
	do_normal_mapping = false;
	do_environment_mapping = false;
	do_blending = false;
	do_disney_brdf = false;
	use_mirror_brdf = polyMesh.is_mirror_brdf;
	phong_spec_exp = polyMesh.phong_spec_exp;

    //printf("Mesh details:\n");
    //printf("   num polys:     %lu\n", polyMesh.polygons.size());
    //printf("   num verts:     %lu\n", polyMesh.vertices.size());
    //printf("   num normals:   %lu\n", polyMesh.normals.size());
    //printf("   num texcoords: %lu\n", polyMesh.texcoords.size());
    
    // hack: move the transform (parsed from the scene json) into the object's position in the scene graph
    // there's definitely a cleaner way to do this since the Mesh class has its own transforms associated with it.
    position = Vector3D(transform[3][0], transform[3][1], transform[3][2]);
    scale = Vector3D(transform[0][0], transform[1][1], transform[2][2]);

	this->vertices.reserve(polyMesh.vertices.size());
	this->normals.reserve(polyMesh.normals.size());
	this->texture_coordinates.reserve(polyMesh.texcoords.size());
    
	for(int i = 0; i < polyMesh.vertices.size(); ++i) {
		Vector3D &u = polyMesh.vertices[i];
		Vector3Df v;
		v.x = u.x;
		v.y = u.y;
		v.z = u.z;
		this->vertices.push_back(v);
	}
	for(int i = 0; i < polyMesh.normals.size(); ++i) {
		Vector3D &u = polyMesh.normals[i];
		Vector3Df v;
		v.x = u.x;
		v.y = u.y;
		v.z = u.z;
		this->normals.push_back(v);
	}
	for(int i = 0; i < polyMesh.texcoords.size(); ++i) {
		Vector2D &u = polyMesh.texcoords[i];
		Vector2Df v;
		v.x = u.x;
		v.y = u.y;
		this->texture_coordinates.push_back(v);
	}
	diffuse_colors.reserve(polygons.size());
	for(int i = 0; i < polygons.size(); ++i) {
		Vector3D &u = polyMesh.material_diffuse_parameters[i];
		Vector3Df v;
		v.x = u.x;
		v.y = u.y;
		v.z = u.z;
		diffuse_colors.push_back(v);
	}

    // these are the buffers that will be handed to glVertexArray calls
	vertexData.reserve(polygons.size() * 3);
	diffuse_colorData.reserve(polygons.size() * 3);
	normalData.reserve(polygons.size() * 3);
	texcoordData.reserve(polygons.size() * 3);
	tangentData.reserve(polygons.size() * 3);

    // populate vertex, normal, and texcoord buffers
	for(int i = 0; i < polygons.size(); ++i) {
		for(int j = 0; j < 3; ++j) {
  			vertexData.push_back(this->vertices[polyMesh.polygons[i].vertex_indices[j]]);
	        diffuse_colorData.push_back(this->diffuse_colors[i]);
            normalData.push_back(this->normals[polyMesh.polygons[i].normal_indices[j]]);
    
		}
		if (this->texture_coordinates.size() > 0) {
			for(int j = 0; j < 3; ++j) {
				texcoordData.push_back(this->texture_coordinates[polyMesh.polygons[i].texcoord_indices[j]]);
			}
		}
	}

	for(int i = 0; i < vertexData.size(); i+=3) {
		Vector3Df v0 = vertexData[i+0];
		Vector3Df v1 = vertexData[i+1];
		Vector3Df v2 = vertexData[i+2];

		Vector2Df uv0 = texcoordData[i+0];
		Vector2Df uv1 = texcoordData[i+1];
		Vector2Df uv2 = texcoordData[i+2];

		Vector3Df deltaPos1;
		deltaPos1.x = v1.x-v0.x;
		deltaPos1.y = v1.y-v0.y;
		deltaPos1.z = v1.z-v0.z;

		Vector3Df deltaPos2;
		deltaPos2.x = v2.x-v0.x;
		deltaPos2.y = v2.y-v0.y;
		deltaPos2.z = v2.z-v0.z;    

		Vector2Df deltaUV1;
		deltaUV1.x = uv1.x - uv0.x;
		deltaUV1.y = uv1.y - uv0.y;

		Vector2Df deltaUV2;
		deltaUV2.x = uv2.x - uv0.x;
		deltaUV2.y = uv2.y - uv0.y;

		float r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);

		Vector3Df tangent;
		tangent.x = (deltaPos1.x * deltaUV2.y - deltaPos2.x * deltaUV1.y)*r;
		tangent.y = (deltaPos1.y * deltaUV2.y - deltaPos2.y * deltaUV1.y)*r;
		tangent.z = (deltaPos1.z * deltaUV2.y - deltaPos2.z * deltaUV1.y)*r;

		this->tangentData.push_back(tangent);
		this->tangentData.push_back(tangent);
		this->tangentData.push_back(tangent);
	}

	glGenBuffers(1, &vertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vector3Df) * vertexData.size(), (void*)&vertexData[0], GL_STATIC_DRAW);

	glGenBuffers(1, &normalBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, normalBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vector3Df) * normalData.size(), (void*)&normalData[0], GL_STATIC_DRAW);
	  
	glGenBuffers(1, &texcoordBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, texcoordBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vector2Df) * texcoordData.size(), (void*)&texcoordData[0], GL_STATIC_DRAW);

	glGenBuffers(1, &tangentBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, tangentBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vector3Df) * tangentData.size(), (void*)&tangentData[0], GL_STATIC_DRAW);

	if (diffuse_colorData.size() > 0) {
		glGenBuffers(1, &diffuse_colorBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, diffuse_colorBuffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(Vector3Df) * diffuse_colorData.size(), (void*)&diffuse_colorData[0], GL_STATIC_DRAW);
	}

	glBindVertexArray(0);

	if (polyMesh.vert_filename != "" && polyMesh.frag_filename != "")
		shaders.push_back(Shader(polyMesh.vert_filename, polyMesh.frag_filename, shader_prefix, shader_prefix));

	uniform_strings = polyMesh.uniform_strings;
	uniform_values = polyMesh.uniform_values;

	if (simple_colors)
        return;
	
    do_disney_brdf = polyMesh.is_disney;

    // create the diffuse albedo texture map
	if (polyMesh.diffuse_filename != "") {
		unsigned int error = lodepng::decode(diffuse_texture, diffuse_texture_width, diffuse_texture_height, polyMesh.diffuse_filename);
		if(error) cerr << "Texture (diffuse) loading error = " << polyMesh.diffuse_filename << endl;
		glGenTextures(1, &diffuseId);
		glBindTexture(GL_TEXTURE_2D, diffuseId);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, diffuse_texture_width, diffuse_texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void *)&diffuse_texture[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		//glGenerateMipmap(GL_TEXTURE_2D);
	    do_texture_mapping = true;
    } else
        do_texture_mapping = false;

    // create the normal map texture map
    if(polyMesh.normal_filename != "") {
		unsigned int error = lodepng::decode(normal_texture, normal_texture_width, normal_texture_height, polyMesh.normal_filename);
		if(error) cerr << "Texture (normal) loading error = " << polyMesh.normal_filename << endl;
		glGenTextures(1, &normalId);
		glBindTexture(GL_TEXTURE_2D, normalId);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, normal_texture_width, normal_texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void *)&normal_texture[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	    do_normal_mapping = true;
    }
    else
        do_normal_mapping = false;

    // create the environment lighting texture map
    if(polyMesh.environment_filename != "") {
		unsigned int error = lodepng::decode(environment_texture, environment_texture_width, environment_texture_height, polyMesh.environment_filename);
		if(error) cerr << "Texture (environment) loading error = " << polyMesh.environment_filename << endl;
		glGenTextures(1, &environmentId);
		glBindTexture(GL_TEXTURE_2D, environmentId);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, environment_texture_width, environment_texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void *)&environment_texture[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	    do_environment_mapping = true;
    } else
        do_environment_mapping = false;

	glBindTexture(GL_TEXTURE_2D, 0);
}

Mesh::~Mesh() {
    glDeleteBuffers(1, &vertexBuffer);
    glDeleteBuffers(1, &normalBuffer);
    glDeleteBuffers(1, &texcoordBuffer);
	glDeleteBuffers(1, &tangentBuffer);

    if (diffuse_colorData.size() > 0)
        glDeleteBuffers(1, &diffuse_colorBuffer);
}

void Mesh::draw_pretty() {
  glPushMatrix();
  glTranslatef(position.x, position.y, position.z);
  glRotatef(rotation.x, 1.0f, 0.0f, 0.0f);
  glRotatef(rotation.y, 0.0f, 1.0f, 0.0f);
  glRotatef(rotation.z, 0.0f, 0.0f, 1.0f);
  glScalef(scale.x, scale.y, scale.z);

  glBindTexture(GL_TEXTURE_2D, 0);
  Spectrum white = Spectrum(1., 1., 1.);
  glMaterialfv(GL_FRONT, GL_DIFFUSE, &white.r);

  // Enable lighting for faces
  glEnable(GL_LIGHTING);
  glDisable(GL_BLEND);
  draw_faces(true, false);

  glPopMatrix();
}

void Mesh::draw() {
	draw_pass(false);
}

void Mesh::draw_shadow() {
	draw_pass(true);
}

void Mesh::draw_pass(bool is_shadow_pass) {
  glPushMatrix();

  glTranslatef(position.x, position.y, position.z);
  glRotatef(rotation.x, 1.0f, 0.0f, 0.0f);
  glRotatef(rotation.y, 0.0f, 1.0f, 0.0f);
  glRotatef(rotation.z, 0.0f, 0.0f, 1.0f);
  glScalef(scale.x, scale.y, scale.z);

  float deg2Rad = M_PI / 180.0;
  
  Matrix4x4 T = Matrix4x4::translation(position);
  Matrix4x4 RX = Matrix4x4::rotation(rotation.x * deg2Rad, Matrix4x4::Axis::X);
  Matrix4x4 RY = Matrix4x4::rotation(rotation.y * deg2Rad, Matrix4x4::Axis::Y);
  Matrix4x4 RZ = Matrix4x4::rotation(rotation.z * deg2Rad, Matrix4x4::Axis::Z);
  Matrix4x4 scaleXform = Matrix4x4::scaling(scale);
  
  Matrix4x4 xform = T * RX * RY * RZ * scaleXform;

  // inv transpose for transforming normals
  Matrix4x4 xformNorm = (RX * RY * RZ * scaleXform).inv().T();    

  // copy to column-major buffers for hand off to OpenGL  
  int idx = 0;
  for (int i=0; i<4; i++) {
      const Vector4D& c = xform.column(i); 
      glObj2World[idx++] = c[0]; glObj2World[idx++] = c[1]; glObj2World[idx++] = c[2]; glObj2World[idx++] = c[3];
  }

  idx = 0;
  for (int i=0; i<3; i++) {
      const Vector4D& c = xformNorm.column(i); 
      glObj2WorldNorm[idx++] = c[0]; glObj2WorldNorm[idx++] = c[1]; glObj2WorldNorm[idx++] = c[2];
  }  
  
  // make an object to shadow light space matrix here
  if (!is_shadow_pass) {

  	for (int light_id=0; light_id < scene->get_num_shadowed_lights(); light_id++) {
	  	Matrix4x4 w2sl = scene->get_world_to_shadowlight(light_id);
	  	Matrix4x4 o2sl = w2sl * xform;
		idx = 0;
	  	for (int i=0; i<4; i++) {
	    	const Vector4D& c = o2sl.column(i); 
	      	glObj2ShadowLight[light_id][idx++] = c[0];
	      	glObj2ShadowLight[light_id][idx++] = c[1];
	      	glObj2ShadowLight[light_id][idx++] = c[2];
	      	glObj2ShadowLight[light_id][idx++] = c[3];
	  	}
	 }
  }

  draw_faces(false, is_shadow_pass);
  glPopMatrix();

}

void Mesh::draw_faces(bool smooth, bool is_shadow_pass) const {

	checkGLError("begin draw faces");

    if (!simple_renderable)
        return;
    
    if (is_shadow_pass) {

    	GLuint shadow_program_id = scene->get_shadow_shader()->_programID;
        glUseProgram(shadow_program_id);

	    int vert_loc = glGetAttribLocation(shadow_program_id, "vtx_position");
	    if (vert_loc >= 0) {
            glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            glVertexAttribPointer(vert_loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(vert_loc);
	    }

    } else {

    	checkGLError("before use program");

        GLuint programID = shaders[0]._programID;

        glUseProgram(programID);

		checkGLError("before bind uniforms");

	    // bind uniforms

        for (int j = 0; j < scene->patterns.size(); ++j) {
            DynamicScene::PatternObject &po = scene->patterns[j];
            int uniformLocation  = glGetUniformLocation(programID, po.name.c_str());
            if (uniformLocation >= 0) {
                if(po.type == 0) {
                    glUniform3f(uniformLocation, po.v.x, po.v.y, po.v.z);
                } else if(po.type == 1) {
                    glUniform1f(uniformLocation, po.s);
                }
            }
        }

		for (int j = 0; j < uniform_strings.size(); ++j) {
			int uniformLocation = glGetUniformLocation(programID, uniform_strings[j].c_str());
			if(uniformLocation >= 0) {
				glUniform1f(uniformLocation, uniform_values[j]);
			}
		}

        int uniformLocation = glGetUniformLocation(programID, "useTextureMapping");
        if(uniformLocation >= 0)
            glUniform1i(uniformLocation, do_texture_mapping ? 1 : 0);

        uniformLocation = glGetUniformLocation(programID, "useNormalMapping");
        if(uniformLocation >= 0)
            glUniform1i(uniformLocation, do_normal_mapping ? 1 : 0);
        
        uniformLocation = glGetUniformLocation(programID, "useEnvironmentMapping");
        if(uniformLocation >= 0)
            glUniform1i(uniformLocation, do_environment_mapping ? 1 : 0);
 
        uniformLocation = glGetUniformLocation(programID, "useMirrorBRDF");
        if(uniformLocation >= 0)
            glUniform1i(uniformLocation, use_mirror_brdf ? 1 : 0);
       	
		uniformLocation = glGetUniformLocation(programID, "spec_exp");
		if(uniformLocation >= 0)
			glUniform1f(uniformLocation, phong_spec_exp);
			
        uniformLocation = glGetUniformLocation(programID, "obj2world");
        if(uniformLocation >= 0)
            glUniformMatrix4fv(uniformLocation, 1, GL_FALSE, glObj2World);
        
        uniformLocation = glGetUniformLocation(programID, "obj2worldNorm");
        if(uniformLocation >= 0)
            glUniformMatrix3fv(uniformLocation, 1, GL_FALSE, glObj2WorldNorm);

        int num_shadowed_lights = scene->get_num_shadowed_lights();
 		for (int i=0; i<num_shadowed_lights; i++) {	
        	string varname = "obj2shadowlight" + std::to_string(i);
        	uniformLocation = glGetUniformLocation(programID, varname.c_str());
        	if(uniformLocation >= 0)
            	glUniformMatrix4fv(uniformLocation, 1, GL_FALSE, glObj2ShadowLight[i]);
        }

        Vector3D camPosition = scene->camera->position();
        float v1 = camPosition.x;
        float v2 = camPosition.y;
        float v3 = camPosition.z;
        uniformLocation = glGetUniformLocation( programID, "camera_position" );
        if (uniformLocation >=0)
        	glUniform3f( uniformLocation, v1, v2, v3 );

        // bind texture samplers ///////////////////////////////////

        int diffuseTextureID = glGetUniformLocation(programID, "diffuseTextureSampler");
        if (diffuseTextureID >= 0) {
	        glActiveTexture(GL_TEXTURE0);
	        glBindTexture(GL_TEXTURE_2D, diffuseId);
            glUniform1i(diffuseTextureID, 0);
        }

        int normalTextureID = glGetUniformLocation(programID, "normalTextureSampler");
        if (normalTextureID >= 0) {
	        glActiveTexture(GL_TEXTURE1);
	        glBindTexture(GL_TEXTURE_2D, normalId);
            glUniform1i(normalTextureID, 1);
        }

        int environmentTextureID = glGetUniformLocation(programID, "environmentTextureSampler");
        if (environmentTextureID >= 0) {
	        glActiveTexture(GL_TEXTURE2);
	        glBindTexture(GL_TEXTURE_2D, environmentId);
            glUniform1i(environmentTextureID, 2);
        }

        for (int i=0; i<num_shadowed_lights; i++) {
        	string varname = "shadowTextureSampler" + std::to_string(i);
            int shadowTextureID  = glGetUniformLocation(programID, varname.c_str());
	        if (shadowTextureID >= 0) {
		        glActiveTexture(GL_TEXTURE3 + i);
		        glBindTexture(GL_TEXTURE_2D, scene->get_shadow_texture(i));
	            glUniform1i(shadowTextureID, 3 + i);
	        }
    	}

        // bind light parameters //////////////////////////////////

        uniformLocation = glGetUniformLocation( programID, "num_directional_lights" );
        if (uniformLocation >= 0)
        	glUniform1i( uniformLocation, scene->directional_lights.size() );

        for (int j = 0; j < scene->directional_lights.size(); ++j) {
            StaticScene::DirectionalLight *light = scene->directional_lights[j];
            float v1 = light->lightDir.x;
            float v2 = light->lightDir.y;
            float v3 = light->lightDir.z;		
            string str = "directional_light_vectors[" + std::to_string(j) + "]";
            uniformLocation = glGetUniformLocation( programID, str.c_str() );
            if (uniformLocation >= 0)
            	glUniform3f( uniformLocation, v1, v2, v3 );
        }

        uniformLocation = glGetUniformLocation( programID, "num_point_lights" );
        if (uniformLocation >= 0)
        	glUniform1i( uniformLocation, scene->point_lights.size() );

        for (int j = 0; j < scene->point_lights.size(); ++j) {
            StaticScene::PointLight *light = scene->point_lights[j];
            float v1 = light->position.x;
            float v2 = light->position.y;
            float v3 = light->position.z;
            string str = "point_light_positions[" + std::to_string(j) + "]";
            uniformLocation = glGetUniformLocation( programID, str.c_str() );
            if(uniformLocation >= 0)
            	glUniform3f( uniformLocation, v1, v2, v3 );
        }

        uniformLocation = glGetUniformLocation( programID, "num_spot_lights" );
        if (uniformLocation >= 0)
        	glUniform1i( uniformLocation, scene->spot_lights.size() );

        for (int j = 0; j < scene->spot_lights.size(); ++j) {
            StaticScene::SpotLight *light = scene->spot_lights[j];
            float v1 = light->position.x;
            float v2 = light->position.y;
            float v3 = light->position.z;
            string str = "spot_light_positions[" + std::to_string(j) + "]";
            uniformLocation = glGetUniformLocation( programID, str.c_str() );
            if(uniformLocation >= 0)
            	glUniform3f( uniformLocation, v1, v2, v3 );

            v1 = light->direction.x;
            v2 = light->direction.y;
            v3 = light->direction.z;
            str = "spot_light_directions[" + std::to_string(j) + "]";
            uniformLocation = glGetUniformLocation( programID, str.c_str() );
            if(uniformLocation >= 0)
            	glUniform3f( uniformLocation, v1, v2, v3 );

            v1 = light->angle;
            str = "spot_light_angles[" + std::to_string(j) + "]";
            uniformLocation = glGetUniformLocation( programID, str.c_str() );
            if(uniformLocation >= 0)
            	glUniform1f( uniformLocation, v1);

            v1 = light->radiance.r;
            v2 = light->radiance.g;
            v3 = light->radiance.b;
            str = "spot_light_intensities[" + std::to_string(j) + "]";
            uniformLocation = glGetUniformLocation( programID, str.c_str() );
            if(uniformLocation >= 0)
            	glUniform3f( uniformLocation, v1, v2, v3 );

        }

        // bind per-vertex attribute buffers  //////////////////////

	    checkGLError("before bind vertex attributes");

	    int vert_loc = glGetAttribLocation(programID, "vtx_position");
	    if (vert_loc >= 0) {
            glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            glVertexAttribPointer(vert_loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(vert_loc);
	    }

	    int dclr_loc = glGetAttribLocation(programID, "vtx_diffuse_color");
	    if (dclr_loc >= 0) {
            glBindBuffer(GL_ARRAY_BUFFER, diffuse_colorBuffer);
            glVertexAttribPointer(dclr_loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(dclr_loc);
	    }

	    int normal_loc = glGetAttribLocation(programID, "vtx_normal");
        if(normal_loc >= 0) {
            glBindBuffer(GL_ARRAY_BUFFER, normalBuffer);
            glVertexAttribPointer(normal_loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(normal_loc);
        }

	    int tex_loc = glGetAttribLocation(programID, "vtx_texcoord");
	    if (tex_loc >= 0) {
            glBindBuffer(GL_ARRAY_BUFFER, texcoordBuffer);
            glVertexAttribPointer(tex_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(tex_loc);
	    }

        int tan_loc = glGetAttribLocation(programID, "vtx_tangent");
        if (tan_loc >= 0) {
            glBindBuffer(GL_ARRAY_BUFFER, tangentBuffer);
            glVertexAttribPointer(tan_loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(tan_loc);
        }
	}

	checkGLError("before glDrawArrays");

	glDrawArrays(GL_TRIANGLES, 0, 3 * polygons.size());

	glUseProgram(0);
	glBindTexture(GL_TEXTURE_2D, 0);

	checkGLError("end draw faces");
}


BBox Mesh::get_bbox() {
  BBox bbox;
  if (simple_renderable) {
	  for(int i = 0; i < vertices.size(); ++i) {
		Vector3Df &u = vertices[i];
		Vector3D v(u.x, u.y, u.z);
		bbox.expand(v);
	  }
	  return bbox;
  }
  return bbox;
}

StaticScene::SceneObject *Mesh::get_static_object() {
  return nullptr;
//  return new StaticScene::Mesh(mesh);
}

Matrix3x3 rotateMatrix(float ux, float uy, float uz, float theta) {
  Matrix3x3 out = Matrix3x3();
  float c = cos(theta);
  float s = sin(theta);
  out(0, 0) = c + ux * ux * (1 - c);
  out(0, 1) = ux * uy * (1 - c) - uz * s;
  out(0, 2) = ux * uz * (1 - c) + uy * s;
  out(1, 0) = uy * ux * (1 - c) + uz * s;
  out(1, 1) = c + uy * uy * (1 - c);
  out(1, 2) = uy * uz * (1 - c) - ux * s;
  out(2, 0) = uz * ux * (1 - c) - uy * s;
  out(2, 1) = uz * uy * (1 - c) + ux * s;
  out(2, 2) = c + uz * uz * (1 - c);
  return out;
}

StaticScene::SceneObject *Mesh::get_transformed_static_object(double t) {
  return nullptr;
}

}  // namespace DynamicScene
}  // namespace CS248
