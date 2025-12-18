#pragma once
#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>

namespace globals
{
	extern bool has_loaded;
    extern bool images_init;

	extern uintptr_t client;
    extern uintptr_t datamodel;
    extern uintptr_t visual_engine;
    extern uintptr_t local_player;
	extern uintptr_t teleport_target;
	extern uintptr_t mouse_service;

	namespace key_info
	{
		extern bool aimbot_active;
		extern bool aimbot_prediction_active;
		extern bool noclip_active;
		extern bool fly_active;
	}

	// Cached list of players updated in a background thread
	extern std::vector<uintptr_t> players_cached;
	extern std::mutex players_mutex;
	extern std::atomic<bool> workers_running;

	struct RenderEntity
	{
		uintptr_t player_instance;
		float screen_x;
		float screen_y;
		float box_left;
		float box_top;
		float box_right;
		float box_bottom;
		float distance;
		int health;
		bool valid;
	};

	extern std::vector<RenderEntity> render_entities;
	extern std::mutex render_mutex;
}