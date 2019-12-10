#include "Mirror.h"
#include "particleContainer/ParticleContainer.h"
#include "Domain.h"
#include "parallel/DomainDecompBase.h"
#include "molecules/Molecule.h"
#include "utils/Logger.h"

#include <fstream>
#include <cmath>
#include <cstdlib>
#include <cstdint>

using namespace std;
using Log::global_log;

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

	for(auto i=0; i<numComponents; ++i) {
		_particleManipCount.deleted.local.at(i)=0;
		_particleManipCount.deleted.global.at(i)=0;
		_particleManipCount.reflected.local.at(i)=0;
		_particleManipCount.reflected.global.at(i)=0;
	}
}

void Mirror::init(ParticleContainer* particleContainer, DomainDecompBase* domainDecomp, Domain* domain) {
	global_log->debug() << "Mirror enabled at position: " << _yPos << std::endl;
}

void Mirror::readXML(XMLfileUnits& xmlconfig)
{
	_pluginID = 100;
	xmlconfig.getNodeValue("pluginID", _pluginID);
	global_log->info() << "Mirror: pluginID = " << _pluginID << endl;

	_yPos = 0.;
	xmlconfig.getNodeValue("yPos", _yPos);
	global_log->info() << "Mirror: y position = " << _yPos << endl;

	_forceConstant = 100.;
	xmlconfig.getNodeValue("forceConstant", _forceConstant);
	global_log->info() << "Mirror: force constant = " << _forceConstant << endl;

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
	if("|<--" == strDirection)
		_direction = MD_LEFT_MIRROR;
	else if("-->|" == strDirection)
		_direction = MD_RIGHT_MIRROR;
	if(MD_LEFT_MIRROR == _direction)
		global_log->info() << "Mirror: direction |<--" << endl;
	else if(MD_RIGHT_MIRROR == _direction)
		global_log->info() << "Mirror: direction -->|" << endl;

	// ratio to reflect
	_ratio = 1.;
	xmlconfig.getNodeValue("ratio", _ratio);

	/** normal distributions */
	if(MT_NORMDISTR_MB == _type)
	{
		bool bRet = true;
		bRet = bRet && xmlconfig.getNodeValue("norm/vxz", _norm.fname.vxz);
		bRet = bRet && xmlconfig.getNodeValue("norm/vy",  _norm.fname.vy);
		if(true == bRet) {  // TODO: move this to method: init()? Has to be called before method: afterForces(), within method Simulation::prepare_start()
			global_log->info() << "Mirror uses MB from files." << std::endl;
			this->readNormDistr();
		}
	}

	/** zero gradient */
	if(MT_ZERO_GRADIENT == _type)
	{
		bool bRet = true;
		bRet = bRet && xmlconfig.getNodeValue("CV/width", _cv.width);
		bRet = bRet && xmlconfig.getNodeValue("CV/margin", _cv.margin);
		bRet = bRet && xmlconfig.getNodeValue("CV/cids/original", _cids.original);
		bRet = bRet && xmlconfig.getNodeValue("CV/cids/forward", _cids.forward);
		bRet = bRet && xmlconfig.getNodeValue("CV/cids/backward", _cids.backward);
		bRet = bRet && xmlconfig.getNodeValue("CV/cids/reflected", _cids.reflected);
		bRet = bRet && xmlconfig.getNodeValue("CV/cids/permitted", _cids.permitted);
		bRet = bRet && xmlconfig.getNodeValue("CV/veloList/numvals", _veloList.numvals);
		bRet = bRet && xmlconfig.getNodeValue("CV/veloList/initvals/x", _veloList.initvals[0]);
		bRet = bRet && xmlconfig.getNodeValue("CV/veloList/initvals/y", _veloList.initvals[1]);
		bRet = bRet && xmlconfig.getNodeValue("CV/veloList/initvals/z", _veloList.initvals[2]);

		if(true == bRet)
		{
			// CV boundaries
			if(MD_LEFT_MIRROR == _direction) {
				_cv.left  = _yPos;
				_cv.right = _cv.left + _cv.width;
			}
			else if(MD_RIGHT_MIRROR == _direction) {
				_cv.right = _yPos;
				_cv.left  = _cv.right - _cv.width;
			}
			_cv.left_outer = _cv.left - _cv.margin;
			_cv.right_outer = _cv.right + _cv.margin;

			// velocity list
			_veloList.list.resize(_veloList.numvals);
			for(auto&& val:_veloList.list) {
				val.at(0) = _veloList.initvals.at(0);
				val.at(1) = _veloList.initvals.at(1);
				val.at(2) = _veloList.initvals.at(2);
			}
/*			for(auto&& val:_veloList.list)
				cout << "v=" << val.at(0) << "," << val.at(1) << "," << val.at(2) << endl; */
		}
	}

	/** Meland2004 */
	if(MT_MELAND_2004 == _type)
	{
		_melandParams.use_probability_factor = true;
		_melandParams.velo_target = 0.4;
		bool bRet = true;
		bRet = bRet && xmlconfig.getNodeValue("meland/use_probability", _melandParams.use_probability_factor);
		bRet = bRet && xmlconfig.getNodeValue("meland/velo_target", _melandParams.velo_target);

		if(not bRet)
		{
			global_log->error() << "Parameters for Meland2004 Mirror provided in config-file *.xml corrupted/incomplete. Prgram exit ..." << endl;
			Simulation::exit(-2004);
		}
	}
}

void Mirror::beforeForces(
		ParticleContainer* particleContainer, DomainDecompBase* domainDecomp,
		unsigned long simstep
) {
	if(MT_ZERO_GRADIENT == _type) {
		for(auto it = particleContainer->iterator(ParticleIterator::ONLY_INNER_AND_BOUNDARY); it.isValid(); ++it) {
			uint32_t cid_zb = it->componentid();
			uint32_t cid_ub = cid_zb+1;
			double ry = it->r(1);
			double vy = it->v(1);

			// no action
			if(ry < _cv.left_outer || ry > _cv.right_outer)
				continue;

			// change back to original component
			if(ry > _cv.left_outer && ry < _cv.left) {
				Component* comp = global_simulation->getEnsemble()->getComponent(_cids.original-1);
				it->setComponent(comp);
			}

			// inside CV
			if(ry > _cv.left && ry < _cv.right) {
				Component* comp;
				if(vy > 0.) {
					comp = global_simulation->getEnsemble()->getComponent(_cids.forward-1);
					it->setComponent(comp);
				}
				if(vy < 0. && cid_ub == _cids.forward) {
					comp = global_simulation->getEnsemble()->getComponent(_cids.backward-1);
					it->setComponent(comp);
					_veloList.list.pop_front();
					std::array<double, 3> v;
					v.at(0) = it->v(0);
					v.at(1) = it->v(1);
					v.at(2) = it->v(2);
					_veloList.list.push_back(v);
				}
			}

			// permitted
			if(cid_ub == _cids.permitted) {
				it->setv(0, 0.);
				it->setv(1, 3.);
				it->setv(2, 0.);
			}
		}
	}
	else if(MT_MELAND_2004 == _type){

		double regionLowCorner[3], regionHighCorner[3];

		// a check only makes sense if the subdomain specified by _direction and _yPos is inside of the particleContainer.
		// if we have an MD_RIGHT_MIRROR: _yPos defines the lower boundary of the mirror, thus we check if _yPos is at
		// most boxmax.
		// if the mirror is an MD_LEFT_MIRROR _yPos defines the upper boundary of the mirror, thus we check if _yPos is
		// at lese boxmin.
		if ((_direction == MD_RIGHT_MIRROR and _yPos < particleContainer->getBoundingBoxMax(1)) or
			(_direction == MD_LEFT_MIRROR and _yPos > particleContainer->getBoundingBoxMin(1))) {
			// if linked cell in the region of the mirror boundary
			for (unsigned d = 0; d < 3; d++) {
				regionLowCorner[d] = particleContainer->getBoundingBoxMin(d);
				regionHighCorner[d] = particleContainer->getBoundingBoxMax(d);
			}

			if (_direction == MD_RIGHT_MIRROR) {
				// ensure that we do not iterate over things outside of the container.
				regionLowCorner[1] = std::max(_yPos, regionLowCorner[1]);
			} else if (_direction == MD_LEFT_MIRROR) {
				// ensure that we do not iterate over things outside of the container.
				regionHighCorner[1] = std::min(_yPos, regionHighCorner[1]);
			}

			// reset local values
			for(auto& it:_particleManipCount.reflected.local)
				it = 0;
			for(auto& it:_particleManipCount.deleted.local)
				it = 0;

			auto begin = particleContainer->regionIterator(regionLowCorner, regionHighCorner, ParticleIterator::ALL_CELLS);  // over all cell types
			for(auto it = begin; it.isValid(); ++it) {
				uint32_t cid_ub = it->componentid()+1;  // unity based componentid --> 0: arbitrary component, 1: first component
				double vy = it->v(1);
				if ( (_direction == MD_RIGHT_MIRROR && vy < 0.) || (_direction == MD_LEFT_MIRROR && vy > 0.) )
					continue;
				double vy_reflected = 2*_melandParams.velo_target - vy;
				if ( (_direction == MD_RIGHT_MIRROR && vy_reflected < 0.) || (_direction == MD_LEFT_MIRROR && vy_reflected > 0.) ) {
					float frnd = 0, pbf = 1.;  // pbf: probability factor, frnd (float): random number [0..1)
					if (true == _melandParams.use_probability_factor) {
						pbf = abs(vy_reflected) / abs(vy);
						frnd = _rnd->rnd();
					}
					// reflect particles and delete all not reflected
					if(frnd < pbf) {
						it->setv(1, vy_reflected);
						_particleManipCount.reflected.local.at(0)++;
						_particleManipCount.reflected.local.at(cid_ub)++;
					}
					else {
						it.deleteCurrentParticle();
						_particleManipCount.deleted.local.at(0)++;
						_particleManipCount.deleted.local.at(cid_ub)++;
					}
				}
				else {
					it.deleteCurrentParticle();
					_particleManipCount.deleted.local.at(0)++;
					_particleManipCount.deleted.local.at(cid_ub)++;
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

void Mirror::VelocityChange( ParticleContainer* particleContainer) {
	double regionLowCorner[3], regionHighCorner[3];

	// a check only makes sense if the subdomain specified by _direction and _yPos is inside of the particleContainer.
	// if we have an MD_RIGHT_MIRROR: _yPos defines the lower boundary of the mirror, thus we check if _yPos is at
	// most boxmax.
	// if the mirror is an MD_LEFT_MIRROR _yPos defines the upper boundary of the mirror, thus we check if _yPos is
	// at lese boxmin.
	if ((_direction == MD_RIGHT_MIRROR and _yPos < particleContainer->getBoundingBoxMax(1)) or
		(_direction == MD_LEFT_MIRROR and _yPos > particleContainer->getBoundingBoxMin(1))) {
		// if linked cell in the region of the mirror boundary
		for (unsigned d = 0; d < 3; d++) {
			regionLowCorner[d] = particleContainer->getBoundingBoxMin(d);
			regionHighCorner[d] = particleContainer->getBoundingBoxMax(d);
		}

		if (_direction == MD_RIGHT_MIRROR) {
			// ensure that we do not iterate over things outside of the container.
			regionLowCorner[1] = std::max(_yPos, regionLowCorner[1]);
		} else if (_direction == MD_LEFT_MIRROR) {
			// ensure that we do not iterate over things outside of the container.
			regionHighCorner[1] = std::min(_yPos, regionHighCorner[1]);
		}

#if defined (_OPENMP)
		#pragma omp parallel shared(regionLowCorner, regionHighCorner)
		#endif
		{
			auto begin = particleContainer->regionIterator(regionLowCorner, regionHighCorner,
														   ParticleIterator::ALL_CELLS);  // over all cell types

			for(auto it = begin; it.isValid(); ++it){
				double additionalForce[3];
				additionalForce[0] = 0;
				additionalForce[2] = 0;
				double ry = it->r(1);
				double vy = it->v(1);
				if(MT_REFLECT == _type || MT_ZERO_GRADIENT == _type) {

					if( (MD_RIGHT_MIRROR == _direction && vy > 0.) || (MD_LEFT_MIRROR == _direction && vy < 0.) ) {

						if(MT_REFLECT == _type)
							it->setv(1, -vy);
						else if(MT_ZERO_GRADIENT == _type) {
							uint32_t cid_ub = it->componentid()+1;
							if(cid_ub != _cids.permitted) {
								float frnd = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
								//cout << "frnd=" << frnd << endl;
								if(frnd <= _ratio) {
									int irnd = rand() % _veloList.numvals;
									//cout << "irnd=" << irnd << endl;
									std::list<std::array<double, 3> > list(_veloList.list);
									/*cout << "VELOLIST" << endl;
									cout << "--------------------------------" << endl;
									for(auto&& val:list)
										cout << "v=" << val.at(0) << "," << val.at(1) << "," << val.at(2) << endl; */
									for(int i=0; i<irnd; i++)
										list.pop_front();
									std::array<double, 3> velo = list.front();
									it->setv(0, velo.at(0) );
									it->setv(1, velo.at(1) );
									it->setv(2, velo.at(2) );
									//cout << "setv=" << velo.at(0) << "," << velo.at(1) << "," << velo.at(2) << endl;

									Component* comp;
                                    comp = global_simulation->getEnsemble()->getComponent(_cids.reflected-1);
                                    it->setComponent(comp);
								}
								else {
									Component* comp;
									comp = global_simulation->getEnsemble()->getComponent(_cids.permitted-1);
									it->setComponent(comp);
									//cout << "PERMITTED" << endl;
								}
							}
						}
						else if(MT_NORMDISTR_MB == _type) {
							double vx_norm = _norm.vxz.front();
							_norm.vxz.pop_front();
							_norm.vxz.push_back(vx_norm);

							double vy_norm = _norm.vy.front();
							_norm.vy.pop_front();
							_norm.vy.push_back(vy_norm);

							double vz_norm = _norm.vxz.front();
							_norm.vxz.pop_front();
							_norm.vxz.push_back(vz_norm);

							it->setv(0, vx_norm);
							it->setv(1, vy_norm);
							it->setv(2, vz_norm);
						}
					}
				}
				else if(MT_FORCE_CONSTANT == _type){
					double distance = _yPos - ry;
					additionalForce[1] = _forceConstant * distance;
//						cout << "additionalForce[1]=" << additionalForce[1] << endl;
//						cout << "before: " << (*it) << endl;
//						it->Fljcenteradd(0, additionalForce);  TODO: Can additional force be added on the LJ sites instead of adding to COM of molecule?
					it->Fadd(additionalForce);
//						cout << "after: " << (*it) << endl;
				}
			}
		}

	}
    if(MT_ZERO_GRADIENT != _type)
        return;

    for(auto it = particleContainer->iterator(ParticleIterator::ALL_CELLS); it.isValid(); ++it) {
        uint32_t cid_zb = it->componentid();
        uint32_t cid_ub = cid_zb+1;

        // permitted
        if(cid_ub == _cids.permitted) {
            it->setv(0, 0.);
            it->setv(1, 3.);
            it->setv(2, 0.);
        }
    }
}

void Mirror::readNormDistr()
{
	struct {
		std::ifstream vxz;
		std::ifstream vy;
	} ifs;
	ifs.vxz.open(_norm.fname.vxz, std::ios::in);
	ifs.vy.open(_norm.fname.vy, std::ios::in);

	//check to see that the file was opened correctly:
	if (!ifs.vxz.is_open() || !ifs.vy.is_open() ) {
		std::cerr << "There was a problem opening the input file!\n";
		Simulation::exit(-1);//exit or do additional error checking
	}

	double dVal = 0.0;
	//keep storing values from the text file so long as data exists:
	while (ifs.vxz >> dVal) {
		_norm.vxz.push_back(dVal);
	}
	while (ifs.vy >> dVal) {
		if(MD_LEFT_MIRROR == _direction)
			_norm.vy.push_back( abs(dVal) );
		else if (MD_RIGHT_MIRROR == _direction)
			_norm.vy.push_back( abs(dVal) * (-1.) );
		else
			Simulation::exit(-1);
	}
	// close files
	ifs.vxz.close();
	ifs.vy.close();
}







