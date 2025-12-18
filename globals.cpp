#include "globals.h"
#include <vector>
#include <mutex>
#include <atomic>

namespace globals
{
	bool has_loaded = false;
	bool images_init = false;

	uintptr_t client;
	uintptr_t datamodel;
	uintptr_t visual_engine;
	uintptr_t local_player;
	uintptr_t teleport_target;	
	uintptr_t mouse_service;

	namespace key_info 
	{
		bool aimbot_active = false;
		bool aimbot_prediction_active = false;
		bool noclip_active = false;
		bool fly_active = false;
	}

	std::vector<uintptr_t> players_cached;
	std::mutex players_mutex;
    std::atomic<bool> workers_running{ false };
    std::vector<RenderEntity> render_entities;
    std::mutex render_mutex;
}