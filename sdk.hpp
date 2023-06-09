#pragma once
#include <cstdint>
#include <Windows.h>

namespace ac {	
	namespace update {
        inline const std::uintptr_t base = 0x400000; // Base used by your reversing tool (IDA, Binja...)
		
        inline const std::uintptr_t localplayer = 0x58AC00;

        inline const std::uintptr_t plrforward = 0x4BFCA0;
        inline const std::uintptr_t plrbackward = 0x4BFD00;
        inline const std::uintptr_t plrleft = 0x4BFC40;
        inline const std::uintptr_t plrright = 0x4BFBF0;
        inline const std::uintptr_t plrjump = 0x4BFA30;
		
        inline const std::uintptr_t checkinput = 0x4EC970;
		
        inline const std::uintptr_t moveplr_fric0 = 0x4C19D3;
        inline const std::uintptr_t moveplr_fric0_sz = 6;
        inline const std::uintptr_t moveplr_fric1 = 0x4C21D5;
        inline const std::uintptr_t moveplr_fric1_sz = 10;
	}

    template <class T>
    inline T rebase( const std::uintptr_t addr ) {
        return ( T )( reinterpret_cast<std::uintptr_t>( GetModuleHandle( NULL ) ) + ( addr - update::base ) );
    }

    template <>
	inline std::uintptr_t rebase( const std::uintptr_t addr ) {
        return reinterpret_cast< std::uintptr_t >( GetModuleHandle( NULL ) ) + ( addr - update::base );
    }

	struct vec3_t
	{
		float x, y, z;
	};

	struct physent_t {
        vec3_t o, vel;
        vec3_t deltapos, newpos;
        float yaw, pitch, roll;        
        float pitchvel;
        float maxspeed;            
        int timeinair;    
        float radius, eyeheight, maxeyeheight, aboveeye; 
        bool inwater;
        bool onfloor, onladder, jumpnext, jumpd, crouching, crouchedinair, trycrouch, cancollide, stuck, scoping;
        int lastjump;
        float lastjumpheight;
        int lastsplash;
        char move, strafe;
        unsigned char state, type;
        float eyeheightvel;
        int last_pos;
	};

    namespace funcs { // those shouldn't need to be updated
        physent_t* get_local_physent( ) {
            return reinterpret_cast<physent_t*>((* ac::rebase<std::uintptr_t*>( ac::update::localplayer ) ) + sizeof( void* ));
        }
    }
}