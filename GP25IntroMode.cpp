#include "GP25IntroMode.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <algorithm>
#include <chrono>
#include <array>

glm::vec3 GP25IntroMode::Spline::interp(float t) const {
	//make sure this looks like [p, (t, t, p)*]
	assert((size() + 2) % 3 == 0);

	//constant extrapolation:
	if (t <= front().first) return front().second;
	if (t >= back().first) return back().second;

	assert(size() >= 4); //if there were only one point in the spline, the two conditions above should have triggered

	size_t i0; //left index of interval

	{ //binary search -- want t in [p0, p1):
		size_t l = 0;
		//l, r, m will be "control point" indices (skipping tangents):
		size_t r = (size() - 1) / 3;

		assert((*this)[3*l].first <= t && t < (*this)[3*r].first); //interval invariant

		while (l + 1 < r) { //until we have a single interval...
			size_t m = (r - l) / 2 + l; //...pick a control point in the middle...
			if ((*this)[3*m].first <= t) { //...and update interval
				l = m;
			} else {
				r = m;
			}
			assert((*this)[3*l].first <= t && t < (*this)[3*r].first); //interval invariant
		}
		i0 = 3*l;

	}

	//binary search found a valid interval:
	assert(i0 + 3 < size());
	assert((*this)[i0].first <= t && t < (*this)[i0+3].first);

	float p; //parameter value which maps to t

	{ //binary search, again:
		float const t0 = (*this)[i0 + 0].first;
		float const t1 = (*this)[i0 + 1].first;
		float const t2 = (*this)[i0 + 2].first;
		float const t3 = (*this)[i0 + 3].first;

		auto val = [t0,t1,t2,t3](float p) {
			float const t01 = (t1 - t0) * p + t0;
			float const t12 = (t2 - t1) * p + t1;
			float const t23 = (t3 - t2) * p + t2;

			float const t012 = (t12 - t01) * p + t01;
			float const t123 = (t23 - t12) * p + t12;

			float const t0123 = (t123 - t012) * p + t012;

			return t0123;
		};

		float l = 0.0;
		float lt = t0;
		float r = 1.0;
		float rt = t3;

		assert(lt <= t && t < rt);

		for (uint32_t step = 0; step < 100; ++step) {
			float m = (r - l) * 0.5f + l; //basic split
			//float m = (r - l) * ((t - lt) / (rt - lt)) + l; //linear estimate; might converge in fewer steps, but does take more computation
			float mt = val(m);
			if (mt <= t) {
				l = m; lt = mt;
			} else {
				r = m; rt = mt;
			}
			assert(lt <= t && t < rt);
			if (std::abs(rt - lt) < 0.001f) break;
		}
		//linear estimate of final parameter value:
		p = (r - l) * ((t - lt) / (rt - lt)) + l;
	}

	//final interpolation:
	glm::vec3 const &v0 = (*this)[i0 + 0].second;
	glm::vec3 const &v1 = (*this)[i0 + 1].second;
	glm::vec3 const &v2 = (*this)[i0 + 2].second;
	glm::vec3 const &v3 = (*this)[i0 + 3].second;

	return v0 * ((1.0f-p)*(1.0f-p)*(1.0f-p))
	     + v1 * (3.0f*((1.0f-p)*(1.0f-p)*p))
	     + v2 * (3.0f*((1.0f-p)*p*p))
	     + v3 * (p*p*p) ;
}

void GP25IntroMode::Spline::set_smooth_tangents() {
	//make sure this looks like [p, (t, t, p)*]
	assert((size() + 2) % 3 == 0);

	//set segments to linear:
	for (size_t i = 0; i + 3 < size(); i += 3) {
		auto const &p0 = (*this)[i+0];
		auto       &p1 = (*this)[i+1];
		auto       &p2 = (*this)[i+2];
		auto const &p3 = (*this)[i+3];

		p1.first = (p3.first - p0.first) * (1.0f / 3.0f) + p0.first;
		p2.first = (p3.first - p0.first) * (2.0f / 3.0f) + p0.first;
		p1.second = (p3.second - p0.second) * (1.0f / 3.0f) + p0.second;
		p2.second = (p3.second - p0.second) * (2.0f / 3.0f) + p0.second;
	}

	//make start and end tangents constant:
	if (size() >= 3) {
		{ //start
			auto const &p0 = (*this)[0].second;
			auto       &p1 = (*this)[1].second;
			p1 = p0;
		}
		{ //end
			auto       &p2 = (*this)[size()-2].second;
			auto const &p3 = (*this)[size()-1].second;
			p2 = p3;
		}
	}

	//make internal tangents have the same direction:
	for (size_t i = 0; i + 6 < size(); i += 3) {

		//auto const &p0 = (*this)[i+0];
		//auto       &p1 = (*this)[i+1];
		auto       &p2 = (*this)[i+2].second;
		auto const &p3 = (*this)[i+3].second;
		auto       &p4 = (*this)[i+4].second;
		//auto       &p5 = (*this)[i+5];
		//auto const &p6 = (*this)[i+6];

		glm::vec3 dir = glm::normalize( (p3 - p2) + (p4 - p3) );
		p2 = glm::length(p2 - p3) * -dir + p3;
		p4 = glm::length(p4 - p3) *  dir + p3;
	}
}

static std::unique_ptr< Sound::Sample > music_sample;

GP25IntroMode::GP25IntroMode(std::function< void() > const &on_finished_) : on_finished(on_finished_) {

	//this little synth thing based on:
	// https://github.com/15-466/15-466-f22-intro/blob/main/GP22IntroMode.cpp

	float bpm = 120.0f;
	{ // ------ music ------
		std::vector< float > data(10 * 48000, 0.0f);

		//triangle wave (lightly phase-modulated):
		[[maybe_unused]] auto triangle = [](float t) -> float {
			float pm = std::sin(t * 0.999f * 2.0f * float(M_PI));
			t += 0.1f * pm;
			t += 0.25f;
			return std::abs(4.0f * (t - std::floor(t)) - 2.0f) - 1.0f;
		};
		//sine wave (lightly phase-modulated + sub-bass):
		[[maybe_unused]] auto sine = [](float t) -> float {
			float pm = std::sin(t * 1.01f * 2.0f * float(M_PI));
			float sub = std::sin(0.5f * t * 2.0f * float(M_PI));
			return 0.5f * std::sin((t + 0.3f * pm) * 2.0f * float(M_PI)) + 0.5f * sub;
		};
		//square wave (lightly pulse-width-modulated):
		[[maybe_unused]] auto square = [](float t) -> float {
			float w = 0.5f + 0.1f * std::sin(t * 0.1f);
			return 0.5f * (t - std::floor(t) > w ? 1.0f : -1.0f);
		};

		constexpr float Attack  = 0.02f;
		constexpr float Decay   = 0.1f;
		constexpr float Sustain = 0.8f;
		constexpr float Release = 0.2f;

		//play synth at given time for given length at given frequency with given wave:
		auto tone = [&](float start, float len, float hz, float vol, std::function< float(float) > const &wave){
			int32_t begin = int32_t(start * 48000);
			int32_t end = begin + int32_t((len + Release) * 48000);

			for (int32_t sample = begin; sample < end; ++sample) {
				float t = (sample - begin + 0.5f) / 48000.0f;

				//envelope value:
				float env;
				if (t < Attack) env = t / Attack;
				else if (t < Attack + Decay) env = ((t - Attack) / Decay) * (Sustain - 1.0f) + 1.0f;
				else if (t <= len) env = Sustain;
				else env = std::min(1.0f, (t - len) / Release) * (0.0f - Sustain) + Sustain;

				//simple, single-oscillator synth:
				float osc1 = wave(t * hz);

				data[sample] += vol * env * osc1;
			}
		};

		//midi note number to frequency (based on A4 being 440hz)
		auto midi2hz = [](float midi) -> float {
			return 440.0f * std::exp2( (midi - 69.0f) / 12.0f );
		};

		[[maybe_unused]] auto C = [](int32_t oct) { return 12.0f + 12.0f * oct; };
		[[maybe_unused]] auto Cs= [](int32_t oct) { return 13.0f + 12.0f * oct; };
		[[maybe_unused]] auto D = [](int32_t oct) { return 14.0f + 12.0f * oct; };
		[[maybe_unused]] auto Ds= [](int32_t oct) { return 15.0f + 12.0f * oct; };
		[[maybe_unused]] auto E = [](int32_t oct) { return 16.0f + 12.0f * oct; };
		[[maybe_unused]] auto F = [](int32_t oct) { return 17.0f + 12.0f * oct; };
		[[maybe_unused]] auto Fs= [](int32_t oct) { return 18.0f + 12.0f * oct; };
		[[maybe_unused]] auto G = [](int32_t oct) { return 19.0f + 12.0f * oct; };
		[[maybe_unused]] auto Gs= [](int32_t oct) { return 20.0f + 12.0f * oct; };
		[[maybe_unused]] auto A = [](int32_t oct) { return 21.0f + 12.0f * oct; };
		[[maybe_unused]] auto As= [](int32_t oct) { return 22.0f + 12.0f * oct; };
		[[maybe_unused]] auto B = [](int32_t oct) { return 23.0f + 12.0f * oct; };
		

		auto tones = [&]( std::function< float(float) > const &wave, float hz, float step, std::string const &score ) {
			for (uint32_t begin = 0; begin < score.size(); /* later */) {
				if (score[begin] == '#' || score[begin] == 'o') {
					uint32_t end = begin + 1;
					while (end < score.size() && score[end] == '=') ++end;
					float vol = (score[begin] == '#' ? 0.4f : 0.2f);
					tone( begin * step, (end - begin - 0.5f) * step, hz, vol, wave );
					begin = end;
				} else {
					++begin;
				}
			}
		};

		//based on a beepbox composition:
		//https://www.beepbox.co/#9n31s6k3l00e00t1Ua7g0fj07r1i0o432T8v1u08f0q0x10r71d23x660W7E3b9080gT1v1u18f0q00d23A0F0B0Q0000Pf600E1617T1v1u99f10k8q0331d4aAcF8BcQ4200P6789E179T4v1uf0f0q011z6666ji8k8k3jSBKSJJAArriiiiii07JCABrzrrrrrrr00YrkqHrsrrrrjr005zrAqzrjzrrqr1jRjrqGGrrzsrsA099ijrABJJJIAzrrtirqrqjqixzsrAjrqjiqaqqysttAJqjikikrizrHtBJJAzArzrIsRCITKSS099ijrAJS____Qg99habbCAYrDzh00E0b40000000000g4g000000014h000000004h400000000p1QFE_x7ghFBUaswzO2CwFO2ewz0jnjhY4t96CLMp9CLXisR_e30000

		//some sort of basic fanfare:
		tones( triangle, midi2hz( Gs(4)), 0.5f * 60.0f / bpm, "1 . . . 2 . . . 3 . . . 4 . . . ");
		tones( triangle, midi2hz( F(4) ), 0.5f * 60.0f / bpm, "1 . . . 2 . . . 3 . . . 4 . . . ");
		tones( triangle, midi2hz( E(4) ), 0.5f * 60.0f / bpm, "1 . . . 2 . . . 3 . . . 4 . . . ");
		tones( triangle, midi2hz( Ds(4)), 0.5f * 60.0f / bpm, "1 . . . 2 . . . 3 . . . o== . . ");
		tones( triangle, midi2hz( Cs(4)), 0.5f * 60.0f / bpm, "1 . . . 2 . . . #===. . 4 . . . ");
		tones( triangle, midi2hz( As(3)), 0.5f * 60.0f / bpm, "1 . . . 2 . #=. 3 . . . 4 . . . ");
		tones( triangle, midi2hz( Gs(3)), 0.5f * 60.0f / bpm, "1 . . . 2 . . . 3 . . . 4 . . . ");
		tones( triangle, midi2hz( F(3) ), 0.5f * 60.0f / bpm, "1 . . . 2 . . . 3 . #===4 . . . ");
		tones( triangle, midi2hz( E(3) ), 0.5f * 60.0f / bpm, "1 . . . 2 . . . 3 . . . 4 . . . ");
		tones( triangle, midi2hz( Ds(3)), 0.5f * 60.0f / bpm, "1 . . . #=. . . 3 . . . o=====. ");
		tones( triangle, midi2hz( Cs(3)), 0.5f * 60.0f / bpm, "1 . . . 2 . . . 3 . . . 4 . . . ");
		tones( triangle, midi2hz( As(2)), 0.5f * 60.0f / bpm, "1 . . . 2 . . . 3 . . . o=======");

		//bass:
		tones( sine,     midi2hz( Ds(2)), 0.5f * 60.0f / bpm, "#=. #=. #=. #=. #=. #=. #=. . . ");


		{ //gently re-center output (remove dc):
			float smoothed = 0.0f;
			for (float &s : data) {
				smoothed += 1e-4f * (s - smoothed);
				s -= smoothed;
			}
		}

		{ //run a basic 'digital reverb' over stuff:
			//use a delay line with a few different taps and feedbacks:
			std::array< float, 2 * 48000 > delay;
			for (auto &s : delay) s = 0.0f; //clear delay line
			uint32_t head = 0;
			auto tap = [&](float offset) -> float {
				return delay[(head + int32_t(delay.size()) - int32_t(std::floor(offset * 48000.0f))) % delay.size()];
			};
			float smoothed = 0.0f;
			for (float &s : data) {
				float wet = 1.0f * s + (
					+ 6.0f * tap(0.43f)
					+ 5.0f * tap(0.21f)
					+ 3.0f * tap(0.13f)
					+ 1.0f * tap(0.09f)
					) / 16.0f;
				smoothed += 0.95f * (wet - smoothed); //smooth off high frequencies before writing reverb buffer
				delay[head] = smoothed;
				head = (head + 1) % delay.size();
				s = 0.1f * tap(0.25f) + 0.9f * s;
			}
		}

		{ //do a low-pass filter on output:
			float smoothed = 0.0f;
			for (float &s : data) {
				smoothed += 0.2f * (s - smoothed);
				s = smoothed;
			}
			smoothed = 0.0f;
			for (float &s : data) {
				smoothed += 0.2f * (s - smoothed);
				s = smoothed;
			}
		}

		{ //fade out the last bit, just in case:
			constexpr uint32_t fade = 2*48000;
			for (uint32_t i = 0; i < fade; ++i) {
				float amt = (i + 0.5f) / float(fade);
				data[data.size()-1-i] *= amt;
			}
		}

		/*{ //DEBUG:
			std::ofstream dump("music-dump.f32", std::ios::binary);
			dump.write(reinterpret_cast< const char * >(data.data()), data.size() * 4);
		}*/


		if (!music_sample) { //<-- when playing repeatedly for testing, want to avoid deallocating sample
			music_sample = std::make_unique< Sound::Sample >(data);
		}

		music = Sound::play(*music_sample);
	}

	//--------------------------------------------

	trace.program = gl_compile_program(
		"#version 330\n"
		"out vec2 Clip;\n"
		"void main() {\n"
		"	vec2 pos = vec2((gl_VertexID & 1) * 4.0 - 1.0, (gl_VertexID & 2) * 2.0 - 1.0);\n"
		"	gl_Position = vec4(pos, 0.0, 1.0 );\n"
		"	Clip = pos;\n"
		"}\n"
	,
		//fragment shader:
		"#version 330\n"
		"#line " STR(__LINE__) "\n"
		"uniform mat3 ORIGIN_FROM_CLIP;\n"
		"uniform mat3 DIRECTION_FROM_CLIP;\n"
		"uniform float LIGHTING;\n"
		"uniform vec3 LIGHT;\n"
		"in vec2 Clip;\n"
		"out vec4 fragColor;\n"
		"int evals = 0;\n"
		"//based on code from Inigo Quilez: https://www.shadertoy.com/view/tl23RK \n"
		"// distance to capped torus. sc is the sine and cosine of half of the total angle of the torus.\n"
		"float sdCappedTorus(in vec3 p, in vec2 sc, in float ra, in float rb) {\n"
		"	p.x = abs(p.x);\n"
		"	float k = (sc.y * p.x > sc.x * p.y) ? dot(p.xy, sc) : length(p.xy);\n"
		"	return sqrt( dot(p,p) + ra*ra - 2.0 * ra * k ) - rb;\n"
		"}\n"
		"float sdTorus(in vec3 p, in float ra, in float rb) {\n"
		"	vec2 q = vec2(length(p.xy) - ra, p.z);\n"
		"	return length(q) - rb;\n"
		"}\n"
		"float sdCapsule( in vec3 p, in vec3 a, in vec3 b, float r ) {\n"
		"	vec3 pa = p - a, ba = b - a;\n"
		"	float h = clamp( dot(pa,ba) / dot(ba,ba), 0.0, 1.0 );\n"
		"	return length( pa - ba*h ) - r;\n"
		"}\n"
		"float sdBox( in vec3 p, in vec3 r ) {\n"
		"	vec3 q = abs(p) - r;\n"
		"	return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);\n"
		"}\n"
		"float sdGP25(in vec3 p, float R) {\n"
		"	float d = 1e6;\n"
		"	{ // G \n"
		"		vec3 pl = p - vec3(-60.0, 60.0, 0.0);\n"
		"		const float r = radians(111.07);\n"
		"		pl.xy = pl.xy * mat2(\n"
		"			cos(r), sin(r),\n"
		"			-sin(r), cos(r)\n"
		"		);\n"
		"		const float ha = radians(158.93);\n"
		"		d = min(d, sdCappedTorus(pl, vec2(sin(ha), cos(ha)), 45.0, R));\n"
		"		d = min(d, sdCapsule(p, vec3(-60.0, 60.0, 0.0), vec3(-15.0, 60.0, 0.0), R));\n"
		"	}\n"
		"	{ // P \n"
		"		d = min(d, sdTorus(p - vec3( 60.0, 60.0, 0.0), 45.0, R));\n"
		"		d = min(d, sdCapsule(p, vec3(15.0, 60.0, 0.0), vec3(15.0,0.0, 0.0), R));\n"
		"	}\n"
		"	{ // 2 \n"
		"		vec3 pl = p - vec3(-60.0,-60.0, 0.0);\n"
		"		const float r = radians(-45.0);\n"
		"		pl.xy = pl.xy * mat2(\n"
		"			cos(r), sin(r),\n"
		"			-sin(r), cos(r)\n"
		"		);\n"
		"		const float ha = radians(135.0);\n"
		"		d = min(d, sdCappedTorus(pl, vec2(sin(ha), cos(ha)), 45.0, R));\n"
		"		d = min(d, sdCapsule(p, vec3(-105.0,-105.0, 0.0), vec3(0.0,-105.0, 0.0), R));\n"
		"	}\n"
		"	{ // 5 \n"
		"		vec3 pl = p - vec3( 60.0,-60.0, 0.0);\n"
		"		const float r = radians(45.0);\n"
		"		pl.xy = pl.xy * mat2(\n"
		"			cos(r), sin(r),\n"
		"			-sin(r), cos(r)\n"
		"		);\n"
		"		const float ha = radians(45.0);\n"
		"		d = min(d, sdCappedTorus(pl, vec2(sin(ha), cos(ha)), 45.0, R));\n"
		"		d = min(d, sdCappedTorus(-pl, vec2(sin(ha), cos(ha)), 45.0, R));\n"
		"		d = min(d, sdCapsule(p, vec3(15.0,-60.0, 0.0), vec3(105.0,-60.0, 0.0), R));\n"
		"		d = min(d, sdCapsule(p, vec3(60.0,-15.0, 0.0), vec3(105.0,-15.0, 0.0), R));\n"
		"		d = min(d, sdCapsule(p, vec3(30.0,-105.0, 0.0), vec3(60.0,-105.0, 0.0), R));\n"
		"	}\n"
		"	return d;\n"
		"}\n"
		"float sdTrack(in vec3 p) {\n"
		"	float d = 1000.0;\n"
		"	//flat platform:\n"
		"	d = min(d, max(abs(p.z- -7.5)-7.5, sdGP25(vec3(p.xy,0.0),15.0)) );\n"
		"	//carve track:\n"
		"	d = max(d, -sdGP25(p - vec3(0.0, 0.0, 15.0), 20.0));\n"
		"	return d;\n"
		"}\n"
		"float sdMarbles(in vec3 p) {\n"
		"	float d = 1000.0;\n"
		"	d = min(d, length(p - vec3(-60.0, 60.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(-26.632, 90.192, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(15.0, 0.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(-105.0,-60.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(-105.0,-105.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(0.0,-105.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(30.0,-105.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(105.0,-15.0, 10.0)) - 15.0);\n"
		"	return d;\n"
		"}\n"
		"float sdScene(in vec3 p) {\n"
		"	evals += 1;\n"
		"	float d = 1000.0;\n"
		"	d = min(d, sdTrack( p ));\n"
		"	//box:\n"
		"	d = min(d, sdBox( p - vec3(0.0, 0.0,-10.5), vec3(105.0, 105.0, 4.5)));\n"
		"	//marbles:\n"
		"	//d = min(d, sdMarbles( p ));\n"
		"	return d;\n"
		"}\n"
		"//after iq: https://iquilezles.org/articles/intersectors/\n"
		"void rtSphere( in vec3 ro, in vec3 rd, in vec3 ce, in float ra, inout float t, inout vec3 normal) {\n"
		"	vec3 oc = ro - ce;\n"
		"	float b = dot( oc, rd );\n"
		"	float c = dot( oc, oc ) - ra*ra;\n"
		"	float h = b*b - c;\n"
		"	if (h > 0.0) {\n"
		"		h = sqrt(h);\n"
		"		float t_min = -b - h;\n"
		"		if (t_min > 0.0 && t_min < t) {\n"
		"			t = t_min;\n"
		"			normal = ro + rd * t - ce;\n"
		"		}\n"
		"	}\n"
		"}\n"
		"bool rtSphereAny( in vec3 ro, in vec3 rd, in vec3 ce, in float ra) {\n"
		"	vec3 oc = ro - ce;\n"
		"	float b = dot( oc, rd );\n"
		"	float c = dot( oc, oc ) - ra*ra;\n"
		"	float h = b*b - c;\n"
		"	if (h > 0.0) {\n"
		"		h = sqrt(h);\n"
		"		float t_max = -b + h;\n"
		"		return t_max > 0.0;\n"
		"	}\n"
		"}\n"
		"void rtMarbles(in vec3 ro, in vec3 rd, inout float t, inout vec3 normal) {\n"
		"	rtSphere(ro, rd, vec3(-60.0, 60.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(-26.632, 90.192, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(15.0, 0.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(-105.0,-60.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(-105.0,-105.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(0.0,-105.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(30.0,-105.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(105.0,-15.0, 10.0), 15.0, t, normal);\n"
		"}\n"
		"bool rtMarblesAny(in vec3 ro, in vec3 rd) {\n"
		"	return\n"
		"	rtSphereAny(ro, rd, vec3(-60.0, 60.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(-26.632, 90.192, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(15.0, 0.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(-105.0,-60.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(-105.0,-105.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(0.0,-105.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(30.0,-105.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(105.0,-15.0, 10.0), 15.0);\n"
		"}\n"
		"void rtSlab(in vec3 ro, in vec3 rd, inout float tmin, inout float tmax) {\n"
		"	const vec3 MIN = vec3(-120.0, -120.0, -15.0);\n"
		"	const vec3 MAX = vec3( 120.0,  120.0, 0.0);\n"
		"	tmin = max(tmin, ((rd.x < 0.0 ? MAX.x : MIN.x) - ro.x) / rd.x);\n"
		"	tmax = min(tmax, ((rd.x < 0.0 ? MIN.x : MAX.x) - ro.x) / rd.x);\n"
		"	tmin = max(tmin, ((rd.y < 0.0 ? MAX.y : MIN.y) - ro.y) / rd.y);\n"
		"	tmax = min(tmax, ((rd.y < 0.0 ? MIN.y : MAX.y) - ro.y) / rd.y);\n"
		"	tmin = max(tmin, ((rd.z < 0.0 ? MAX.z : MIN.z) - ro.z) / rd.z);\n"
		"	tmax = min(tmax, ((rd.z < 0.0 ? MIN.z : MAX.z) - ro.z) / rd.z);\n"
		"}\n"
		"vec3 Albedo(in vec3 p) {\n"
		"	float d = 1000.0;\n"
		"	vec3 c = vec3(1.0, 0.0, 1.0);\n"
		"	float dt = sdTrack( p );\n"
		"	if (dt < d) {\n"
		"		d = dt;\n"
		"		c = vec3( 0.922, 0.309, 0.150 );\n"
		"	}\n"
		"	float db = sdBox( p - vec3(0.0, 0.0,-10.5), vec3(105.0, 105.0, 4.5));\n"
		"	if (db < d) {\n"
		"		d = db;\n"
		"		c = vec3( 0.687, 0.939, 0.565 );\n"
		"	}\n"
		"	return c;\n"
		"}\n"
		"//from https://iquilezles.org/articles/normalsSDF/ :\n"
		"vec3 sdNormal( in vec3 p ) {\n"
		"	const float h = 0.0005;\n"
		"	const vec2 k = vec2(1.0,-1.0)*0.57735;\n"
		"	return normalize(\n"
		"		k.xyy*sdScene( p + k.xyy*h ) +\n"
		"		k.yyx*sdScene( p + k.yyx*h ) +\n"
		"		k.yxy*sdScene( p + k.yxy*h ) +\n"
		"		k.xxx*sdScene( p + k.xxx*h )\n"
		"	);\n"
		"}\n"
		"void main() {\n"
		"	vec3 Origin = ORIGIN_FROM_CLIP * vec3(Clip, 1.0);\n"
		"	vec3 Direction = DIRECTION_FROM_CLIP * vec3(Clip, 1.0);\n"
		"	vec3 l = normalize(LIGHT);\n"
		"	vec3 rd = normalize(Direction);\n"
		"	vec3 ro = Origin;\n"
		"	const float FAR = 500.0;\n"
		"	//intersection info:\n"
		"	float t = FAR;\n"
		"	vec3 normal = vec3(0.0, 0.0, 0.0);\n"
		"	vec3 albedo = vec3(1.0, 0.0, 1.0);\n"
		"	{ //check vs marbles:\n"
		"		rtMarbles(ro, rd, t, normal);\n"
		"		if (t != FAR) {\n"
		"			albedo = vec3( 1.0, 0.604, 0.0 );\n"
		"		}\n"
		"	}\n"
		"	{ //check vs slab:\n"
		"		float tmin = 0.0;\n"
		"		float tmax = t;\n"
		"		rtSlab(ro, rd, tmin, tmax);\n" //limit to the range where the slab is
		"		if (tmin < tmax) {\n"
		"			for ( int i = 0; i < 256; ++i) {\n"
		"				vec3 pos = ro + tmin*rd;\n"
		"				float h = sdScene(pos);\n"
		"				if (h < 0.0001 || tmin > tmax) break;\n"
		"				tmin += h;\n"
		"			}\n"
		"			if (tmin < tmax) {\n"
		"				t = tmin;\n"
		"				vec3 p = ro + t * rd;\n"
		"				normal = sdNormal(p);\n"
		"				albedo = Albedo(p);\n"
		"			}\n"
		"		}\n"
		"	}\n"
		"	if (t < FAR) {\n"
		"		//hit something in the scene. Do lighting:\n"
		"		vec3 n = normalize(normal);\n"
		"		vec3 e = vec3(0.2, 0.2, 0.22) * (0.5 + 0.5 * dot(n, vec3(0,0,1))); //sky\n"
		"		vec3 so = ro + t * rd + n * 0.1;\n"
		"		vec3 sd = l;\n"
		"		float s = FAR;\n"
		"		vec3 unused;\n"
		"		if (rtMarblesAny(so, sd)) s = 0;\n"
		"		if (s == FAR) {\n"
		"			//if no marble hits, look for a hit in the slab:\n"
		"			float smin = 0.0;\n"
		"			float smax = s;\n"
		"			rtSlab(so, sd, smin, smax);\n" //limit to the range where the slab is
		"			if (smin < smax) {\n"
		"				for ( int i = 0; i < 64; ++i) {\n"
		"					vec3 pos = so + smin*sd;\n"
		"					float h = sdScene(pos);\n"
		"					if (h < 0.0001 || smin > smax) break;\n"
		"					smin += h;\n"
		"				}\n"
		"				if (smin < smax) {\n"
		"					s = smin;\n"
		"				}\n"
		"			}\n"
		"		}\n"
		"		if (s == FAR) {\n"
		"			e += 1.2 * vec3(1.0, 1.0, 0.95) * max(0, dot(n, l)); //sun\n"
		"		}\n"
		"		fragColor = vec4(mix(vec3(1.0), e, LIGHTING) * albedo, 1.0);\n"
		"	} else {\n"
		"		//horizon/sky:\n"
		"		fragColor = vec4(mix( vec3(1.0, 1.0, 1.0), vec3(0.0, 0.0, 0.0), clamp((rd.z + 0.1) / 0.2, 0.0, 1.0)), 1.0);\n"
		"	}\n"
		"}\n"
	);

	//look up the locations of uniforms:
	trace.ORIGIN_FROM_CLIP_mat3 = glGetUniformLocation(trace.program, "ORIGIN_FROM_CLIP");
	trace.DIRECTION_FROM_CLIP_mat3 = glGetUniformLocation(trace.program, "DIRECTION_FROM_CLIP");
	trace.LIGHTING_float = glGetUniformLocation(trace.program, "LIGHTING");
	trace.LIGHT_vec3 = glGetUniformLocation(trace.program, "LIGHT");

	glGenVertexArrays(1, &empty_vao);
	glBindVertexArray(empty_vao);
	//nothing to configure
	glBindVertexArray(0);

	GL_ERRORS();

	//--------------------------------------------
	//camera path

	//fly through from A to B:
	glm::vec2 A = glm::vec2(-100.0f, -140.0f);
	glm::vec2 B = glm::vec2( 140.0f,  240.0f);

	anim.camera_at.emplace_back(0.0f, glm::vec3(0.0f, 0.0f, 240.0f));
		anim.camera_at.emplace_back(0.666f, glm::vec3(0.0f, 0.0f, 240.0f));
		anim.camera_at.emplace_back(1.333f, glm::vec3(0.0f, 0.0f, 240.0f));
	anim.camera_at.emplace_back(2.0f, glm::vec3(0.0f, 0.0f, 240.0f));
		anim.camera_at.emplace_back(2.333f, glm::vec3(0.0f, 0.0f, 240.0f));
		anim.camera_at.emplace_back(3.000f, glm::vec3(-80.0f,-120.0f, 50.0f) + glm::vec3(10.0f, 20.0f, 0.0f));
	anim.camera_at.emplace_back(3.5f,     glm::vec3(A, 50.0f));
		anim.camera_at.emplace_back(3.5f,     glm::vec3(A, 50.0f));
		anim.camera_at.emplace_back(4.0f,     glm::vec3(A, 10.0f));
	anim.camera_at.emplace_back(4.0f,     glm::vec3(A, 10.0f));
		anim.camera_at.emplace_back(5.0f,     glm::vec3(glm::mix(A, B, 0.3f), 12.0f));
		anim.camera_at.emplace_back(7.0f,     glm::vec3(glm::mix(A, B, 0.6f), 10.0f));
	anim.camera_at.emplace_back(9.5f,     glm::vec3(B, 15.0f));
		anim.camera_at.emplace_back(9.7f,   glm::vec3( B + (B - A) * 0.2f, 15.0f));
		anim.camera_at.emplace_back(9.9f,   glm::vec3( B + (B - A) * 0.4f, 100.0f));
	anim.camera_at.emplace_back(10.0f,    glm::vec3(B + (B - A) * 0.6f, 300.0f));

	anim.camera_up.emplace_back(0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
		anim.camera_up.emplace_back();
		anim.camera_up.emplace_back();
	anim.camera_up.emplace_back(2.0f, glm::vec3(0.0f, 1.0f, 0.0f));
		anim.camera_up.emplace_back();
		anim.camera_up.emplace_back();
	anim.camera_up.emplace_back(3.5f, glm::vec3(0.0f, -0.02f, 1.0f));
		anim.camera_up.emplace_back();
		anim.camera_up.emplace_back();
	anim.camera_up.emplace_back(5.0f, glm::vec3(0.0f, 0.01f, 1.0f));
		anim.camera_up.emplace_back();
		anim.camera_up.emplace_back();
	anim.camera_up.emplace_back(6.0f, glm::vec3(0.0f, 0.2f, 1.0f));
		anim.camera_up.emplace_back();
		anim.camera_up.emplace_back();
	anim.camera_up.emplace_back(6.7f, glm::vec3(0.0f, 0.0f, 1.0f));

	anim.camera_up.set_smooth_tangents();

	anim.camera_target.emplace_back(0.0f, glm::vec3(0.0f, 0.0f, 0.0f));
		anim.camera_target.emplace_back();
		anim.camera_target.emplace_back();
	anim.camera_target.emplace_back(3.0f, glm::vec3(0.0f, 0.0f, 0.0f));
		anim.camera_target.emplace_back();
		anim.camera_target.emplace_back();
	anim.camera_target.emplace_back(8.0f, glm::vec3(300.0f, 300.0f, 40.0f));
		anim.camera_target.emplace_back();
		anim.camera_target.emplace_back();
	anim.camera_target.emplace_back(9.5f, glm::vec3(300.0f, 300.0f, 500.0f));

	anim.camera_target.set_smooth_tangents();

}

GP25IntroMode::~GP25IntroMode() {
}

void GP25IntroMode::benchmark() {
	std::cout << "benchmark" << std::endl;

	glFlush();
	glFinish();

	int w,h;
	SDL_GetWindowSizeInPixels(Mode::window, &w, &h);
	glm::uvec2 drawable_size = glm::uvec2(w, h);
	glViewport(0, 0, drawable_size.x, drawable_size.y);


	for (uint32_t iter = 0; iter < 4; ++iter) {
		double total = 0.0;
		double min = std::numeric_limits< double >::infinity();
		double max =-std::numeric_limits< double >::infinity();
		uint32_t frames = 10 * 30; //not as complete but faster to wait for
		for (uint32_t f = 0; f < frames; ++f) {
			auto before = std::chrono::high_resolution_clock::now();
			time = f / float(frames) * 10.0f;
			draw(drawable_size);
			glFlush();
			glFinish();
			auto after = std::chrono::high_resolution_clock::now();
			double ms = std::chrono::duration< double >(after - before).count() * 1000.0;
			total += ms;
			min = std::min(min, ms);
			max = std::max(max, ms);
		}
		std::cout << min << " " << total / double(frames) << " " << max << std::endl;
	}
}

bool GP25IntroMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_PRINTSCREEN) {
			//ignore the "printscreen" key:
			return false;
		} else {
			finish();
			return true;
		}
	}
	return false;
}

void GP25IntroMode::update(float elapsed) {
	time += elapsed;
	if (time > 10.0f) {
		finish();
		return;
	}
}

void GP25IntroMode::draw(glm::uvec2 const &drawable_size) {

	glm::vec3 camera_at = anim.camera_at.interp(time);
	glm::vec3 camera_up = anim.camera_up.interp(time);
	glm::vec3 camera_target = anim.camera_target.interp(time);

	//camera 'inward' direction: toward the target:
	glm::vec3 camera_in = glm::normalize(camera_target - camera_at);

	//camera 'upward' direction: perpendicular to 'in' direction:
	camera_up = glm::normalize(camera_up - glm::dot(camera_in, camera_up) * camera_in);

	//camera 'rightward' direction: perpendicular to 'in' and 'up':
	glm::vec3 camera_right = glm::cross(camera_in, camera_up);

	const float fov = 60.0f; //fov, minimum direction

	//image radius at one unit from the sensor:
	const float radius = std::tan(glm::radians(fov) / 2.0f);

	//matrix that puts [-1,1] x [-1,1] x {1} to ray directions:
	glm::mat3 direction_from_clip = glm::mat3(
		camera_right * (radius * float(drawable_size.x) / float(std::min(drawable_size.x, drawable_size.y))),
		camera_up * (radius * float(drawable_size.y) / float(std::min(drawable_size.x, drawable_size.y))),
		camera_in
	);

	glm::mat3 origin_from_clip = glm::mat3(
		glm::vec3(0.0f),
		glm::vec3(0.0f),
		camera_at
	);

	{ //ortho testing:
		float ortho_radius = radius * 240.0f;
		glm::mat3 ortho_direction_from_clip = glm::mat3(
			glm::vec3(0.0f),
			glm::vec3(0.0f),
			camera_in
		);
		glm::mat3 ortho_origin_from_clip = glm::mat3(
			camera_right * (ortho_radius * float(drawable_size.x) / float(std::min(drawable_size.x, drawable_size.y))),
			camera_up * (ortho_radius * float(drawable_size.y) / float(std::min(drawable_size.x, drawable_size.y))),
			camera_at
		);

		float blend = std::clamp(-(time - 2.5f) / 0.5f, 0.0f, 1.0f);

		direction_from_clip += (ortho_direction_from_clip - direction_from_clip) * blend;
		origin_from_clip += (ortho_origin_from_clip - origin_from_clip) * blend;
	}

	//Clear background:
	glClearColor(0.2f, 0.7f, 0.3f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	glUseProgram(trace.program);
	glUniformMatrix3fv(trace.DIRECTION_FROM_CLIP_mat3, 1, GL_FALSE, glm::value_ptr(direction_from_clip));
	glUniformMatrix3fv(trace.ORIGIN_FROM_CLIP_mat3, 1, GL_FALSE, glm::value_ptr(origin_from_clip));
	float lighting_fade = std::clamp((time - 1.5f) / 1.5f, 0.0f, 1.0f);
	glUniform1f(trace.LIGHTING_float, lighting_fade);
	glm::vec3 light = glm::normalize(glm::mix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(2.0f, 1.0f, 0.5f), lighting_fade));
	glUniform3fv(trace.LIGHT_vec3, 1, glm::value_ptr(light));

	glBindVertexArray(empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);

	glUseProgram(0);

	GL_ERRORS();
}


void GP25IntroMode::finish() {
	if (loop_forever) {
		if (music) music->set_volume(0.0f, 1.0f / 10.0f);
		music = Sound::play(*music_sample);
		time = 0.0f;
	} else {
		music->set_volume(0.0f, 1.0f / 10.0f);
		std::function< void() > on_finished_ = on_finished;
		Mode::set_current(nullptr);
		if (on_finished_) on_finished_();
	}
}
