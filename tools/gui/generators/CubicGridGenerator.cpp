/*
 * CubicGridGenerator.cpp
 *
 *  Created on: Mar 25, 2011
 *      Author: kovacevt, eckhardw
 */

#include "CubicGridGenerator.h"
#include "Parameters/ParameterWithIntValue.h"
#include "Parameters/ParameterWithLongIntValue.h"
#include "Parameters/ParameterWithBool.h"
#include "common/MardynConfigurationParameters.h"
#include "common/PrincipalAxisTransform.h"
#include "molecules/Molecule.h"
#include "Tokenize.h"
#include "utils/Timer.h"
#include <cstring>

#ifndef MARDYN
extern "C" {

	Generator* create_generator() {
		return new CubicGridGenerator();
	}

	void destruct_generator(Generator* generator) {
		delete generator;
	}
}
#endif

CubicGridGenerator::CubicGridGenerator() :
	MDGenerator("CubicGridGenerator"), _numMolecules(4), _molarDensity(0.6),
	_temperature(300. / 315774.5), _binaryMixture(false) {

	_components.resize(1);
	_components[0].addLJcenter(0, 0, 0, 1.0, 1.0, 1.0, 0.0, false);
	calculateSimulationBoxLength();
}

vector<ParameterCollection*> CubicGridGenerator::getParameters() {
	vector<ParameterCollection*> parameters;
	parameters.push_back(new MardynConfigurationParameters(_configuration));

	ParameterCollection* tab = new ParameterCollection("EqvGridParameters", "Parameters of EqvGridGenerator",
			"Parameters of EqvGridGenerator", Parameter::BUTTON);
	parameters.push_back(tab);

	tab->addParameter(
			new ParameterWithDoubleValue("molarDensity", "Molar density [mol/l]",
					"molar density in mol/l", Parameter::LINE_EDIT,
					false, _molarDensity));
	tab->addParameter(
			new ParameterWithLongIntValue("numMolecules", "Number of Molecules",
					"Total number of Molecules", Parameter::LINE_EDIT,
					false, _numMolecules));
	tab->addParameter(
			new ParameterWithDoubleValue("temperature", "Temperature [K]",
					"Temperature in the domain in Kelvin", Parameter::LINE_EDIT,
					false, _temperature / MDGenerator::kelvin_2_mardyn ));
	tab->addParameter(
			new ComponentParameters("component1", "component1",
					"Set up the parameters of component 1", _components[0]));
	tab->addParameter(
			new ParameterWithBool("binaryMixture", "Binary Mixture",
					"Check this option to simulate a binary mixture.\n(A second component will be added.)",
					Parameter::CHECKBOX, true, _binaryMixture));
	if (_binaryMixture) {
		tab->addParameter(
				new ComponentParameters("component2", "component2",
						"Set up the parameters of component 2", _components[1]));
	}
	return parameters;
}


void CubicGridGenerator::setParameter(Parameter* p) {
	string id = p->getNameId();
	if (id == "numMolecules") {
		_numMolecules = static_cast<ParameterWithLongIntValue*> (p)->getValue();
		calculateSimulationBoxLength();
	} else if (id == "molarDensity") {
		_molarDensity = static_cast<ParameterWithDoubleValue*> (p)->getValue();
		calculateSimulationBoxLength();
	} else if (id == "temperature") {
		_temperature = static_cast<ParameterWithDoubleValue*> (p)->getValue() * MDGenerator::kelvin_2_mardyn;
	} else if (id.find("component1") != std::string::npos) {
		std::string part = remainingSubString(".", id);
		ComponentParameters::setParameterValue(_components[0], p, part);
	} else if (id == "binaryMixture") {
		_binaryMixture = static_cast<ParameterWithBool*>(p)->getValue();
		if (_binaryMixture && _components.size() == 1) {
			_components.resize(2);
			_components[1].addLJcenter(0, 0, 0, 1.0, 1.0, 1.0, 5.0, false);
		} else if (!_binaryMixture && _components.size() == 2) {
			_components.resize(1);
		}
	} else if (id.find("component2") != std::string::npos) {
		std::string part = remainingSubString(".", id);
		ComponentParameters::setParameterValue(_components[1], p, part);
	} else if (firstSubString(".", id) == "ConfigurationParameters") {
		std::string part = remainingSubString(".", id);
		MardynConfigurationParameters::setParameterValue(_configuration, p, part);
	}
}


void CubicGridGenerator::calculateSimulationBoxLength() {
	// 1 mol/l = 0.6022 / nm^3 = 0.0006022 / Ang^3 = 0.089236726516 / a0^3
	double parts_per_a0 = _molarDensity * MDGenerator::molPerL_2_mardyn;
	double volume = _numMolecules / parts_per_a0;
	_simBoxLength = pow(volume, 1./3.);
}


void CubicGridGenerator::readPhaseSpaceHeader(Domain* domain, double timestep) {
	_logger->info() << "Reading PhaseSpaceHeader from CubicGridGenerator..." << endl;
	domain->setCurrentTime(0);

	domain->disableComponentwiseThermostat();
	domain->setGlobalTemperature(_temperature);
	domain->setGlobalLength(0, _simBoxLength);
	domain->setGlobalLength(1, _simBoxLength);
	domain->setGlobalLength(2, _simBoxLength);

	for (unsigned int i = 0; i < _components.size(); i++) {
		Component component = _components[i];
		if (_configuration.performPrincipalAxisTransformation()) {
			principalAxisTransform(component);
		}
		domain->addComponent(component);
	}
	domain->setepsilonRF(1e+10);
	_logger->info() << "Reading PhaseSpaceHeader from CubicGridGenerator done." << endl;

    /* silence compiler warnings */
    (void) timestep;
}


unsigned long CubicGridGenerator::readPhaseSpace(ParticleContainer* particleContainer,
		std::list<ChemicalPotential>* lmu, Domain* domain, DomainDecompBase* domainDecomp) {

	Timer inputTimer;
	inputTimer.start();
	_logger->info() << "Reading phase space file (CubicGridGenerator)." << endl;

// create a body centered cubic layout, by creating by placing the molecules on the
// vertices of a regular grid, then shifting that grid by spacing/2 in all dimensions.

	int numMoleculesPerDimension = pow((double) _numMolecules / 2.0, 1./3.);
	_components[0].updateMassInertia();
	if (_binaryMixture) {
		_components[1].updateMassInertia();
	}


	unsigned long int id = 1;
	double spacing = _simBoxLength / numMoleculesPerDimension;
	double origin = spacing / 4.; // origin of the first DrawableMolecule

	int start_i = floor((domainDecomp->getBoundingBoxMin(0, domain) / _simBoxLength) * numMoleculesPerDimension) - 1;
	int start_j = floor((domainDecomp->getBoundingBoxMin(1, domain) / _simBoxLength) * numMoleculesPerDimension) - 1;
	int start_k = floor((domainDecomp->getBoundingBoxMin(2, domain) / _simBoxLength) * numMoleculesPerDimension) - 1;

	int end_i = ceil((domainDecomp->getBoundingBoxMax(0, domain) / _simBoxLength) * numMoleculesPerDimension) + 1;
	int end_j = ceil((domainDecomp->getBoundingBoxMax(1, domain) / _simBoxLength) * numMoleculesPerDimension) + 1;
	int end_k = ceil((domainDecomp->getBoundingBoxMax(2, domain) / _simBoxLength) * numMoleculesPerDimension) + 1;

	// only for console output
	double percentage = 1.0 / ((end_i - start_i) * 2.0) * 100.0;
	int percentageRead = 0;

	for (int i = start_i; i < end_i; i++) {
		for (int j = start_j; j < end_j; j++) {
			for (int k = start_k; k < end_k; k++) {

				double x = origin + i * spacing;
				double y = origin + j * spacing;
				double z = origin + k * spacing;
				if (domainDecomp->procOwnsPos(x,y,z, domain)) {
					addMolecule(x, y, z, id, particleContainer);
				}
				// increment id in any case, because this particle will probably
				// be added by some other process
				id++;
			}
		}
		if ((int)(i * percentage) > percentageRead) {
			percentageRead = i * percentage;
			_logger->info() << "Finished reading molecules: " << (percentageRead) << "%\r" << flush;
		}
	}

	origin = spacing / 4. * 3.; // origin of the first DrawableMolecule

	for (int i = start_i; i < end_i; i++) {
		for (int j = start_j; j < end_j; j++) {
			for (int k = start_k; k < end_k; k++) {
				double x = origin + i * spacing;
				double y = origin + j * spacing;
				double z = origin + k * spacing;
				if (domainDecomp->procOwnsPos(x,y,z, domain)) {
					addMolecule(x, y, z, id, particleContainer);
				}
				// increment id in any case, because this particle will probably
				// be added by some other process
				id++;
			}
		}
		if ((int)(50 + i * percentage) > percentageRead) {
			percentageRead = 50 + i * percentage;
			_logger->info() << "Finished reading molecules: " << (percentageRead) << "%\r" << flush;
		}
	}
	removeMomentum(particleContainer, _components);
	domain->evaluateRho(particleContainer->getNumberOfParticles(), domainDecomp);
	_logger->info() << "Calculated Rho=" << domain->getglobalRho() << endl;
	inputTimer.stop();
	_logger->info() << "Initial IO took:                 " << inputTimer.get_etime() << " sec" << endl;
	return id;
}

void CubicGridGenerator::addMolecule(double x, double y, double z, unsigned long id, ParticleContainer* particleContainer) {
	vector<double> velocity = getRandomVelocity(_temperature);

	//double orientation[4] = {1, 0, 0, 0}; // default: in the xy plane
	// rotate by 30° along the vector (1/1/0), i.e. the angle bisector of x and y axis
	// o = cos 30° + (1 1 0) * sin 15°
	//double orientation[4];
	//getOrientation(15, 10, orientation);

//	int componentType = 0;
//	if (_binaryMixture) {
//		componentType = randdouble(0, 1.999999);
//	}

	double I[3] = {0.,0.,0.};
	I[0] = _components[0].I11();
	I[1] = _components[0].I22();
	I[2] = _components[0].I33();
	/*****  Copied from animake - initialize anular velocity *****/
//	double w[3];
//	for(int d=0; d < 3; d++) {
//		w[d] = (I[d] == 0)? 0.0: ((randdouble(0,1) > 0.5)? 1: -1) *
//				sqrt(2.0* randdouble(0,1)* _temperature / I[d]);
//		w[d] = w[d] * MDGenerator::fs_2_mardyn;
//	}
	/************************** End Copy **************************/

	Molecule m(id, x, y, z, // position
			velocity[0], -velocity[1], velocity[2] // velocity
		);
	particleContainer->addParticle(m);
}


bool CubicGridGenerator::validateParameters() {
	bool valid = true;

	if (_configuration.getScenarioName() == "") {
		valid = false;
		_logger->error() << "ScenarioName not set!" << endl;
	}

	if (_configuration.getOutputFormat() == MardynConfiguration::XML) {
		valid = false;
		_logger->error() << "OutputFormat XML not yet supported!" << endl;
	}

	if (_simBoxLength < 2. * _configuration.getCutoffRadius()) {
		valid = false;
		_logger->error() << "Cutoff radius is too big (there would be only 1 cell in the domain!)" << endl;
		_logger->error() << "Cutoff radius=" << _configuration.getCutoffRadius()
							<< " domain size=" << _simBoxLength << endl;
	}
	return valid;
}


