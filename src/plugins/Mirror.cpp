#include "Mirror.h"
#include "particleContainer/ParticleContainer.h"
#include "Domain.h"
#include "parallel/DomainDecompBase.h"
#include "molecules/Molecule.h"
#include "utils/Logger.h"
#include "plugins/NEMD/DistControl.h"

#include <string>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <cstdint>

using namespace std;
using Log::global_log;

void printInsertionStatus(std::pair<std::map<uint64_t,double>::iterator,bool> &status)
{
	if (status.second == false)
		std::cout << "Element already existed";
	else
		std::cout << "Element successfully inserted";
	std::cout << " with a value of " << status.first->second << std::endl;
}

Mirror::Mirror() :
	_pluginID(100)
{
	// random numbers
	DomainDecompBase& domainDecomp = global_simulation->domainDecomposition();
	int nRank = domainDecomp.getRank();
	_rnd.reset(new Random(8624+nRank));

	uint32_t numComponents = global_simulation->getEnsemble()->getComponents()->size();
	_particleManipCount.deleted.local.resize(numComponents+1);
	_particleManipCount.deleted.global.resize(numComponents+1);
	_particleManipCount.reflected.local.resize(numComponents+1);
	_particleManipCount.reflected.global.resize(numComponents+1);

	for(uint32_t i=0; i<numComponents; ++i) {
		_particleManipCount.deleted.local.at(i)=0;
		_particleManipCount.deleted.global.at(i)=0;
		_particleManipCount.reflected.local.at(i)=0;
		_particleManipCount.reflected.global.at(i)=0;
	}
}

void Mirror::init(ParticleContainer* particleContainer, DomainDecompBase* domainDecomp, Domain* domain) {
	global_log->debug() << "[Mirror] Enabled at position: " << _position.coord << std::endl;
}

void Mirror::readXML(XMLfileUnits& xmlconfig)
{
	_pluginID = 100;
	xmlconfig.getNodeValue("pluginID", _pluginID);
	global_log->info() << "[Mirror] pluginID = " << _pluginID << endl;

	// Target component
	_targetComp = 0;
	xmlconfig.getNodeValue("cid", _targetComp);
	if(_targetComp > 0) { global_log->info() << "[Mirror] Target component: " << _targetComp << endl; }

	// Mirror position
	_position.axis = 1;  // only y-axis supported yet
	_position.coord = 0.;
	_position.ref.id = 0;  // 0:domain origin, 1:left interface, 2:right interface
	_position.ref.origin = 0.;
	_position.ref.coord = 0.;
	int id = 0;
	xmlconfig.getNodeValue("position/refID", id);
	_position.ref.id = (uint16_t)(id);
	xmlconfig.getNodeValue("position/coord", _position.ref.coord);
	SubjectBase* subject = getSubject();
	this->update(subject);
	if(_position.ref.id > 0) {
		if(nullptr != subject)
			subject->registerObserver(this);
		else {
			global_log->error() << "[Mirror] Initialization of plugin DistControl is needed before! Program exit..." << endl;
			Simulation::exit(-1);
		}
	}
	global_log->info() << "[Mirror] Enabled at position: y = " << _position.coord << endl;

	/** mirror type */
	_type = MT_UNKNOWN;
	uint32_t type = 0;
    xmlconfig.getNodeValue("@type", type);
    _type = static_cast<MirrorType>(type);

	/** mirror direction */
	_direction = MD_LEFT_MIRROR;
	uint32_t dir = 0;
	xmlconfig.getNodeValue("direction", dir);
	_direction = static_cast<MirrorDirection>(dir);
	std::string strDirection = "unknown";
	xmlconfig.getNodeValue("@dir", strDirection);
	if("|-o" == strDirection)
		_direction = MD_LEFT_MIRROR;
	else if("o-|" == strDirection)
		_direction = MD_RIGHT_MIRROR;
	if(MD_LEFT_MIRROR == _direction)
		global_log->info() << "[Mirror] Reflect particles to the right |-o" << endl;
	else if(MD_RIGHT_MIRROR == _direction)
		global_log->info() << "[Mirror] Reflect particles to the left o-|" << endl;

	/** constant force */
	if(MT_FORCE_CONSTANT == _type)
	{
		_forceConstant = 100.;
		xmlconfig.getNodeValue("forceConstant", _forceConstant);
		global_log->info() << "[Mirror] Applying force in vicinity of mirror: _forceConstant = " << _forceConstant << endl;
	}

	/** zero gradient */
	if(MT_ZERO_GRADIENT == _type)
	{
		global_log->error() << "[Mirror] Method 3 (MT_ZERO_GRADIENT) is deprecated. Use 5 (MT_MELAND_2004) instead. Program exit ..." << std::endl;
		Simulation::exit(-1);
	}

	/** normal distributions */
	if(MT_NORMDISTR_MB == _type)
	{
		global_log->error() << "[Mirror] Method 4 (MT_NORMDISTR_MB) is deprecated. Use 5 (MT_MELAND_2004) instead. Program exit ..." << std::endl;
		Simulation::exit(-1);
	}

	/** Meland2004 */
	if(MT_MELAND_2004 == _type)
	{
		xmlconfig.getNodeValue("meland/fixed_probability", _melandParams.fixed_probability_factor);

		if(!xmlconfig.getNodeValue("meland/velo_target", _melandParams.velo_target))
		{
			global_log->error() << "[Mirror] Meland: Parameters for method 5 (MT_MELAND_2004) provided in config-file *.xml corrupted/incomplete. Program exit ..." << endl;
			Simulation::exit(-2004);
		}
		else {
			global_log->info() << "[Mirror] Meland: target velocity = " << _melandParams.velo_target << std::endl;
			if (_melandParams.fixed_probability_factor > 0) {
				global_log->info() << "[Mirror] Meland: FixedProb = " << _melandParams.fixed_probability_factor << std::endl;
			}
		}
	}
	
	if(MT_RAMPING == _type)
	{
		bool bRet = true;
		bRet = bRet && xmlconfig.getNodeValue("ramping/start", _rampingParams.startStep);
		bRet = bRet && xmlconfig.getNodeValue("ramping/stop", _rampingParams.stopStep);
		bRet = bRet && xmlconfig.getNodeValue("ramping/treatment", _rampingParams.treatment);
		
		if (not bRet) {
			global_log->error() << "[Mirror] Ramping: Parameters for method 5 (MT_RAMPING) provided in config-file *.xml corrupted/incomplete. Program exit ..." << endl;
			Simulation::exit(-1);
		}
		else {
			if(_rampingParams.startStep > _rampingParams.stopStep) {
				global_log->error() << "[Mirror] Ramping: Start > Stop. Program exit ..." << endl;
				Simulation::exit(-1);
			}
			else {
				global_log->info() << "[Mirror] Ramping from " << _rampingParams.startStep << " to " << _rampingParams.stopStep << endl;
				std::string treatmentStr = "";
				switch(_rampingParams.treatment) {
					case 0 : treatmentStr = "Deletion";
						break;
					case 1 : treatmentStr = "Transmission";
						break;
					default:
						global_log->error() << "[Mirror] Ramping: No proper treatment was set. Use 0 (Deletion) or 1 (Transmission). Program exit ..." << endl;
						Simulation::exit(-1);
				}
				global_log->info() << "[Mirror] Ramping: Treatment for non-reflected particles: " << _rampingParams.treatment << " ( " << treatmentStr << " ) " << endl;
			}
		}
	}
	
	/** Diffuse mirror **/
	_diffuse_mirror.enabled = false;
	_diffuse_mirror.width = 0;
	bool bRet = xmlconfig.getNodeValue("diffuse/width", _diffuse_mirror.width);
	_diffuse_mirror.enabled = bRet;
	if(_diffuse_mirror.width > 0.0) { global_log->info() << "[Mirror] Using diffuse Mirror width = " << _diffuse_mirror.width << endl; }
}

void Mirror::beforeForces(
		ParticleContainer* particleContainer, DomainDecompBase* domainDecomp,
		unsigned long simstep
) {
	if(MT_MELAND_2004 == _type){

		double regionLowCorner[3], regionHighCorner[3];

		// a check only makes sense if the subdomain specified by _direction and _position.coord is inside of the particleContainer.
		// if we have an MD_RIGHT_MIRROR: _position.coord defines the lower boundary of the mirror, thus we check if _position.coord is at
		// most boxmax.
		// if the mirror is an MD_LEFT_MIRROR _position.coord defines the upper boundary of the mirror, thus we check if _position.coord is
		// at lese boxmin.
		if ((_direction == MD_RIGHT_MIRROR and _position.coord-_diffuse_mirror.width < particleContainer->getBoundingBoxMax(1)) or
			(_direction == MD_LEFT_MIRROR and _position.coord+_diffuse_mirror.width > particleContainer->getBoundingBoxMin(1))) {
			// if linked cell in the region of the mirror boundary
			for (unsigned d = 0; d < 3; d++) {
				regionLowCorner[d] = particleContainer->getBoundingBoxMin(d);
				regionHighCorner[d] = particleContainer->getBoundingBoxMax(d);
			}

			if (_direction == MD_RIGHT_MIRROR) {
				// ensure that we do not iterate over things outside of the container.
				regionLowCorner[1] = std::max(_position.coord-_diffuse_mirror.width, regionLowCorner[1]);
			} else if (_direction == MD_LEFT_MIRROR) {
				// ensure that we do not iterate over things outside of the container.
				regionHighCorner[1] = std::min(_position.coord+_diffuse_mirror.width, regionHighCorner[1]);
			}

			// reset local values
			for(auto& it:_particleManipCount.reflected.local)
				it = 0;
			for(auto& it:_particleManipCount.deleted.local)
				it = 0;

			auto begin = particleContainer->regionIterator(regionLowCorner, regionHighCorner, ParticleIterator::ALL_CELLS);  // over all cell types
			for(auto it = begin; it.isValid(); ++it) {
				uint32_t cid_ub = it->componentid()+1;  // unity based componentid --> 0: arbitrary component, 1: first component
				
				if ((_targetComp != 0) and (cid_ub != _targetComp)) { continue; }
				
				double vy = it->v(1);
				if ( (_direction == MD_RIGHT_MIRROR && vy < 0.) || (_direction == MD_LEFT_MIRROR && vy > 0.) )
					continue;
				/** Diffuse Mirror **/
				if (_diffuse_mirror.enabled == true) {
					uint64_t pid = it->getID();
					double ry = it->r(1);
					double mirror_pos;
					auto search = _diffuse_mirror.pos_map.find(pid);
					if (search != _diffuse_mirror.pos_map.end() ) {
						mirror_pos = search->second;
					}
					else {
						float frnd = _rnd->rnd();
						if (_direction == MD_RIGHT_MIRROR)
							mirror_pos = _position.coord - static_cast<double>(frnd) * _diffuse_mirror.width;
						else
							mirror_pos = _position.coord + static_cast<double>(frnd) * _diffuse_mirror.width;
						std::pair<std::map<uint64_t,double>::iterator,bool> status;
						status = _diffuse_mirror.pos_map.insert({pid, mirror_pos});
#ifndef NDEBUG
						printInsertionStatus(status);
#endif
					}
					
					if (ry <= mirror_pos)
						continue;
					else {
						auto search = _diffuse_mirror.pos_map.find(pid);
						_diffuse_mirror.pos_map.erase(search);
					}
				}
				double vy_reflected = 2*_melandParams.velo_target - vy;
				if ( (_direction == MD_RIGHT_MIRROR && vy_reflected < 0.) || (_direction == MD_LEFT_MIRROR && vy_reflected > 0.) ) {
					float frnd = 0, pbf = 1.;  // pbf: probability factor, frnd (float): random number [0..1)
					if (_melandParams.fixed_probability_factor > 0) {
						pbf = _melandParams.fixed_probability_factor;
					}
					else {
						pbf = std::abs(vy_reflected / vy);
					}
					frnd = _rnd->rnd();
					global_log->debug() << "[Mirror] Meland: pbf = " << pbf << " ; frnd = " << frnd << " ; vy_reflected = " << vy_reflected << " ; vy = " << vy << endl;
					// reflect particles and delete all not reflected
					if(frnd < pbf) {
						it->setv(1, vy_reflected);
						_particleManipCount.reflected.local.at(0)++;
						_particleManipCount.reflected.local.at(cid_ub)++;
					}
					else {
						particleContainer->deleteMolecule(it, false);
						_particleManipCount.deleted.local.at(0)++;
						_particleManipCount.deleted.local.at(cid_ub)++;
					}
				}
				else {
					particleContainer->deleteMolecule(it, false);
					_particleManipCount.deleted.local.at(0)++;
					_particleManipCount.deleted.local.at(cid_ub)++;
				}
			}
		}
	}
	else if(MT_RAMPING == _type){

		double regionLowCorner[3], regionHighCorner[3];

		// a check only makes sense if the subdomain specified by _direction and _position.coord is inside of the particleContainer.
		// if we have an MD_RIGHT_MIRROR: _position.coord defines the lower boundary of the mirror, thus we check if _position.coord is at
		// most boxmax.
		// if the mirror is an MD_LEFT_MIRROR _position.coord defines the upper boundary of the mirror, thus we check if _position.coord is
		// at lese boxmin.
		if ((_direction == MD_RIGHT_MIRROR and _position.coord < particleContainer->getBoundingBoxMax(1)) or
			(_direction == MD_LEFT_MIRROR and _position.coord > particleContainer->getBoundingBoxMin(1))) {
			// if linked cell in the region of the mirror boundary
			for (unsigned d = 0; d < 3; d++) {
				regionLowCorner[d] = particleContainer->getBoundingBoxMin(d);
				regionHighCorner[d] = particleContainer->getBoundingBoxMax(d);
			}

			if (_direction == MD_RIGHT_MIRROR) {
				// ensure that we do not iterate over things outside of the container.
				regionLowCorner[1] = std::max(_position.coord, regionLowCorner[1]);
			} else if (_direction == MD_LEFT_MIRROR) {
				// ensure that we do not iterate over things outside of the container.
				regionHighCorner[1] = std::min(_position.coord, regionHighCorner[1]);
			}
			
			// reset local values
			for(auto& it:_particleManipCount.reflected.local)
				it = 0;
			for(auto& it:_particleManipCount.deleted.local)
				it = 0;

			auto begin = particleContainer->regionIterator(regionLowCorner, regionHighCorner, ParticleIterator::ALL_CELLS);  // over all cell types
			for(auto it = begin; it.isValid(); ++it) {
				
				uint32_t cid_ub = it->componentid()+1;  // unity based componentid --> 0: arbitrary component, 1: first component

				if ((_targetComp != 0) and (cid_ub != _targetComp)) { continue; }

				double vy = it->v(1);
				
				if ( (_direction == MD_RIGHT_MIRROR && vy < 0.) || (_direction == MD_LEFT_MIRROR && vy > 0.) )
					continue;
				
				float ratioRefl;
				float frnd = _rnd->rnd();
				uint64_t currentSimstep = global_simulation->getSimulationStep();
				
				if(currentSimstep <= _rampingParams.startStep) {
					ratioRefl = 1;
				}
				else if ((currentSimstep > _rampingParams.startStep) && (currentSimstep < _rampingParams.stopStep)) {
					ratioRefl = ((float)(_rampingParams.stopStep - currentSimstep) / (float)(_rampingParams.stopStep - _rampingParams.startStep));
				}
				else {
					ratioRefl = 0;
				}
				
				if(frnd <= ratioRefl) {
					it->setv(1, -vy);
					_particleManipCount.reflected.local.at(0)++;
					_particleManipCount.reflected.local.at(cid_ub)++;
					global_log->debug() << "[Mirror] Ramping: Velo. reversed at step " << currentSimstep << " , ReflRatio: " << ratioRefl << endl;
				}
				else {
					if (_rampingParams.treatment == 0) {
						// Delete particle
						particleContainer->deleteMolecule(it, false);
						_particleManipCount.deleted.local.at(0)++;
						_particleManipCount.deleted.local.at(cid_ub)++;
					}
					else if (_rampingParams.treatment == 1) {
						// Transmit particle
					}
				}
			}
		}
	}
}

void Mirror::afterForces(
        ParticleContainer* particleContainer, DomainDecompBase* domainDecomp,
        unsigned long simstep
)
{
	this->VelocityChange(particleContainer);
}

SubjectBase* Mirror::getSubject()
{
	SubjectBase* subject = nullptr;
	std::list<PluginBase*>& plugins = *(global_simulation->getPluginList() );
	for (auto&& pit:plugins) {
		std::string name = pit->getPluginName();
		if(name == "DistControl") {
			subject = dynamic_cast<SubjectBase*>(pit);
		}
	}
	return subject;
}

void Mirror::update(SubjectBase* subject)
{
	auto* distControl = dynamic_cast<DistControl*>(subject);
	double dMidpointLeft, dMidpointRight;
	dMidpointLeft = dMidpointRight = 0.;
	if(nullptr != distControl) {
		dMidpointLeft = distControl->GetInterfaceMidLeft();
		dMidpointRight = distControl->GetInterfaceMidRight();
	}

	switch(_position.ref.id) {
	case 0:
		_position.ref.origin = 0.;
		break;
	case 1:
		_position.ref.origin = dMidpointLeft;
		break;
	case 2:
		_position.ref.origin = dMidpointRight;
		break;
	default:
		_position.ref.origin = 0.;
	}
	_position.coord = _position.ref.origin + _position.ref.coord;
}

void Mirror::VelocityChange( ParticleContainer* particleContainer) {
	double regionLowCorner[3], regionHighCorner[3];

	// a check only makes sense if the subdomain specified by _direction and _position.coord is inside of the particleContainer.
	// if we have an MD_RIGHT_MIRROR: _position.coord defines the lower boundary of the mirror, thus we check if _position.coord is at
	// most boxmax.
	// if the mirror is an MD_LEFT_MIRROR _position.coord defines the upper boundary of the mirror, thus we check if _position.coord is
	// at lese boxmin.
	if ((_direction == MD_RIGHT_MIRROR and _position.coord < particleContainer->getBoundingBoxMax(1)) or
		(_direction == MD_LEFT_MIRROR and _position.coord > particleContainer->getBoundingBoxMin(1))) {
		// if linked cell in the region of the mirror boundary
		for (unsigned d = 0; d < 3; d++) {
			regionLowCorner[d] = particleContainer->getBoundingBoxMin(d);
			regionHighCorner[d] = particleContainer->getBoundingBoxMax(d);
		}

		if (_direction == MD_RIGHT_MIRROR) {
			// ensure that we do not iterate over things outside of the container.
			regionLowCorner[1] = std::max(_position.coord, regionLowCorner[1]);
		} else if (_direction == MD_LEFT_MIRROR) {
			// ensure that we do not iterate over things outside of the container.
			regionHighCorner[1] = std::min(_position.coord, regionHighCorner[1]);
		}

#if defined (_OPENMP)
		#pragma omp parallel shared(regionLowCorner, regionHighCorner)
#endif
		{
			auto begin = particleContainer->regionIterator(regionLowCorner, regionHighCorner,
														   ParticleIterator::ALL_CELLS);  // over all cell types

			for(auto it = begin; it.isValid(); ++it){

				double vy = it->v(1);
				uint32_t cid_ub = it->componentid()+1;

				if ((_targetComp != 0) and (cid_ub != _targetComp)) { continue; }
				
				if(MT_REFLECT == _type) {
					if( (MD_RIGHT_MIRROR == _direction && vy > 0.) || (MD_LEFT_MIRROR == _direction && vy < 0.) ) {
						it->setv(1, -vy);
					}
				}
				else if(MT_FORCE_CONSTANT == _type){
					double additionalForce[3];
					additionalForce[0] = 0;
					additionalForce[2] = 0;
					double ry = it->r(1);
					double distance = _position.coord - ry;
					additionalForce[1] = _forceConstant * distance;
//						cout << "[Mirror] additionalForce[1]=" << additionalForce[1] << endl;
//						cout << "[Mirror] before: " << (*it) << endl;
//						it->Fljcenteradd(0, additionalForce);  TODO: Can additional force be added on the LJ sites instead of adding to COM of molecule?
					it->Fadd(additionalForce);
//						cout << "[Mirror] after: " << (*it) << endl;
				}
			}
		}
	}
}
