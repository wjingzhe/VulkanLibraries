#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

class Camera
{
public:
	enum CameraType {lookat,firstperson};
	CameraType cameraType = CameraType::lookat;

	glm::vec3 rotation = glm::vec3();
	glm::vec3 _position = glm::vec3();
	glm::vec4 viewPos = glm::vec4();

	float rotationSpeed = 1.0f;
	float movementSpeed = 1.0f;

	bool updated = false;
	bool flipY = false;

	struct  
	{
		glm::mat4 perspective;
		glm::mat4 view;
	}matrices;

	struct  
	{
		bool left = false;
		bool right = false;
		bool up = false;
		bool down = false;
	}keys;

	bool moving()
	{
		return keys.left || keys.right || keys.up || keys.down;
	}

	float getNearClip()
	{
		return znear;
	}

	float getFarClip()
	{
		return zfar;
	}

	void setPerspective(float fov, float aspect,float znear,float zfar)
	{
		this->fov = fov;
		this->znear = znear;
		this->zfar = zfar;
		matrices.perspective = glm::perspective(glm::radians(fov), aspect, znear, zfar);
		if (flipY)
		{
			matrices.perspective[1][1] *= -1.0f;
		}
	}

	void updateAspectRatio(float aspect)
	{
		matrices.perspective = glm::perspective(glm::radians(fov), aspect, znear, zfar);
		if (flipY)
		{
			matrices.perspective[1][1] *= -1.0f;
		}
	}

	void setPosition(glm::vec3 position)
	{
		this->_position = position;
		updateViewMatrix();
	}

	void setRotation(glm::vec3 rotation)
	{
		this->rotation = rotation;
		updateViewMatrix();
	}

	void rotate(glm::vec3 delta)
	{
		this->rotation += delta;
		updateViewMatrix();
	}

	void setTranslation(glm::vec3 translation)
	{
		this->_position = translation;
		updateViewMatrix();
	}

	void translate(glm::vec3 delta)
	{
		this->_position += delta;
		updateViewMatrix();
	}

	void setRotationSpeed(float rotationSpeed)
	{
		this->rotationSpeed = rotationSpeed;
	}

	void update(float deltaTime)
	{
		updated = false;
		if (cameraType == CameraType::firstperson)
		{
			 if (moving())
			 {
				 glm::vec3 camFront;
				 camFront.x = -cos(glm::radians(rotation.x))*sin(glm::radians(rotation.y));
				 camFront.y = sin(glm::radians(rotation.x));
				 camFront.z = cos(glm::radians(rotation.x))*cos(glm::radians(rotation.y));
				 camFront = glm::normalize(camFront);

				 float moveSpeed = deltaTime * movementSpeed;

				 if (keys.up)
				 {
					 _position += camFront * moveSpeed;
				 }
				 if (keys.down)
				 {
					 _position -= camFront * moveSpeed;
				 }
				 if (keys.left)
				 {
					 _position -= glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f)))*moveSpeed;
				 }
				 if (keys.right)
				 {
					 _position += glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f)))*moveSpeed;
				 }

				 updateViewMatrix();
			 }
		}
	}

	// Update camera passing separate axis data(gamepad)
	// Return true if view or position has been changed
	bool updatePad(glm::vec2 axisLeft, glm::vec2 axisRight, float deltaTime)
	{
		bool retVal = false;

		if (cameraType == CameraType::firstperson)
		{
			//Use the common console thumbstick layout
			// Left = view,right = move

			const float deadZone = 0.00015f;
			const float range = 1.0f - deadZone;

			glm::vec3 camFront;
			camFront.x = -cos(glm::radians(rotation.x))*sin(glm::radians(rotation.y));
			camFront.y = sin(glm::radians(rotation.x));
			camFront.z = cos(glm::radians(rotation.x))*cos(glm::radians(rotation.y));
			camFront = glm::normalize(camFront);

			float moveSpeed = deltaTime * movementSpeed*2.0f;
			float rotSpeed = deltaTime * rotationSpeed*50.0f;

			//Move
			if (fabsf(axisLeft.y) > deadZone)
			{
				float pos = (fabsf(axisLeft.y) - deadZone) / range;
				_position -= camFront * pos*((axisLeft.y < 0.0f) ? -1.0f : 1.0f)*moveSpeed;
				retVal = true;
			}

			if (fabsf(axisLeft.x)>deadZone)
			{
				float pos = (fabsf(axisLeft.x) - deadZone) / range;
				_position += glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f))) *pos*((axisLeft.x < 0.0f) ? -1.0f : 1.0f)*moveSpeed;
				retVal = true;
			}

			// Rotate
			if (fabsf(axisRight.x) > deadZone)
			{
				float pos = (fabsf(axisRight.x) - deadZone) / range;
				rotation.y += pos * ((axisRight.x < 1.0f) ? -1.0f : 1.0f)*rotSpeed;
				retVal = true;
			}
			if (fabsf(axisRight.y)>deadZone)
			{
				float pos = (fabsf(axisRight.y) - deadZone) / range;
				rotation.x -= pos * ((axisRight.y < 0.0f) ? -1.0f : 1.0f)*rotSpeed;
				retVal = true;
			}
		}
		else
		{
			//todo: move code from example bass class for look-at
		}

		if (retVal)
		{
			updateViewMatrix();
		}

		return retVal;
	}//

protected:
	void updateViewMatrix()
	{
		glm::mat4 rotM = glm::mat4(1.0f);
		glm::mat4 transM;

		rotM = glm::rotate(rotM, glm::radians(rotation.x*(flipY ? 1.0f : 1.0f)), glm::vec3(1.0f, 0.0f, 0.0f));
		rotM = glm::rotate(rotM, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		rotM = glm::rotate(rotM, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		glm::vec3 translation = _position;
		if (flipY)
		{
			translation.y *= -1.0f;
		}
		transM = glm::translate(glm::mat4(1.0f), translation);

		if (cameraType == CameraType::firstperson)
		{
			matrices.view = rotM * transM;
		}
		else
		{
			matrices.view = transM * rotM;
		}

		viewPos = glm::vec4(_position, 0.0f)*glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);
		updated = true;
	}

private:
	float fov;
	float znear, zfar;
};