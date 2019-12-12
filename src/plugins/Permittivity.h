/*
 * Permittivity.h
 *
 *  Created on: November 2019
 *      Author: Joshua Marx
 */
 
 // DESCRIPTION: Samples the relative permittivity for pure Stockmayer fluids or mixtures thereof
 
#ifndef SRC_PLUGINS_PERMITTIVITY_H_
#define SRC_PLUGINS_PERMITTIVITY_H_

#include "Domain.h"
#include "parallel/DomainDecompBase.h"
#include "particleContainer/ParticleContainer.h"
#include "plugins/PluginBase.h"

class Permittivity: public PluginBase {
public:

	void init(ParticleContainer* particleContainer, DomainDecompBase* domainDecomp, Domain* domain) override;
	void readXML(XMLfileUnits& xmlconfig) override;
	void record(ParticleContainer* particleContainer);
	void endStep(ParticleContainer* particleContainer, DomainDecompBase* domainDecomp, Domain* domain,
				 unsigned long simstep) override;
	void reset();
	void collect(DomainDecompBase* domainDecomp);
	void output(Domain* domain, unsigned long timestep);
	void finish(ParticleContainer* particleContainer, DomainDecompBase* domainDecomp, Domain* domain) override{};
	std::string getPluginName() override { return std::string("Permittivity"); }
	static PluginBase* createInstance() { return new Permittivity();}
	
	
	
private:
	unsigned long _writeFrequency;      // Write frequency for all profiles -> Length of recording frame before output
	unsigned long _initStatistics;      // Timesteps to skip at start of the simulation
	unsigned long _recordingTimesteps;  // Record every Nth timestep during recording frame
	unsigned long _accumulatedSteps;    // Number of steps in a block
	unsigned long _totalNumTimeSteps;	// Total number of time steps
	unsigned long _numParticlesLocal;   // Counts number of particles considered for each block locally
	unsigned _numOutputs;               // Number of output values to be written
	unsigned _numComponents;            // Number of components
	unsigned _currentOutputNum;         // Number of current block
	std::map<unsigned,double> _myAbs;   // Dipole moment of component
	std::map<unsigned,double> _permittivity; // Dielectric constant
	std::map<unsigned,unsigned long> _numParticles; // Counts number of particles considered for each block globally
	std::map<unsigned,double> _outputSquaredM; // Total average squared dipole moment for each block
	std::map<unsigned, std::map<unsigned, double>> _outputM; // Total average dipole moment for each block
	std::map<unsigned long, std::map<unsigned long, double >> _localM; // Total dipole moment local
	std::map<unsigned long, std::map<unsigned long, double >> _globalM; // Total dipole moment global
	double _totalAverageM[3]; // Total dipole moment averaged over whole production run
	double _totalAverageSquaredM; // Total squared dipole moment averaged over whole production run
	std::string _outputPrefix;


};

#endif /*SRC_PLUGINS_PERMITTIVITY_H_*/
