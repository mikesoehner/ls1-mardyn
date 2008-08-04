#include "Simulation.h"
#include "Domain.h"
#include "molecules/Molecule.h"
#include "datastructures/LinkedCells.h"
#include "datastructures/AdaptiveSubCells.h"
#include "parallel/DomainDecompBase.h"
#include "parallel/DomainDecompDummy.h"
#ifdef PARALLEL
#include "parallel/DomainDecomposition.h"
#endif
#include "datastructures/adapter/ParticlePairs2PotForceAdapter.h"
#include "integrators/Integrator.h"
#include "integrators/Leapfrog.h"

#include <fstream>
#include <iostream>
#include <iterator>

#ifdef NEW_IO
// removing this include will use a fallback command line parser
#include <boost/program_options.hpp>
namespace po = boost::program_options;
#endif
template<class T> ostream& operator<<(ostream& os, const vector<T>& v)
{
  copy(v.begin(), v.end(), ostream_iterator<T>(cout, " "));
  return os;
}

utils::Log Simulation::_log("Simulation");

Simulation::Simulation(int *argc, char ***argv)
{
#ifdef PARALLEL
  _domainDecomposition = (parallel::DomainDecompBase*) new parallel::DomainDecomposition(argc, argv);
#else
  _domainDecomposition
      = (parallel::DomainDecompBase*) new parallel::DomainDecompDummy();
#endif
  int ownrank = _domainDecomposition->getRank();

#ifndef NEW_IO
  if (*argc != 4)
  {
    if (ownrank == 0)
    {
      cout << "Usage: " << *argv[0]
          << " <configfilename> <number of timesteps> <outputprefix>" << endl;
    }
    delete _domainDecomposition;
    exit(1);
  }

  // open filestream to the input file
  string inputfilename((*argv)[1]);
  ifstream inputfilestream(inputfilename.c_str());

  std::string inputPath;
  std::cout << inputfilename << std::endl;
  int lastIndex = inputfilename.find_last_of('/',inputfilename.size()-1);
  if (lastIndex == string::npos)
  {
    inputPath="";
  }    
  else 
  {
    inputPath = inputfilename.substr(0, lastIndex+1);
  }    
  
  // store number of timesteps to be simulated
  _numberOfTimesteps = atol((*argv)[2]);

  // store prefix for output files
  _outputPrefix = string((*argv)[3]);

  // used to store one token of the inputfilestream
  string token;
  string phaseSpaceFileName;
  double timestepLength;

  _domain = new Domain(ownrank);
  _particlePairsHandler = new datastructures::ParticlePairs2PotForceAdapter(*_domain);

  // The first line of the config file has to contain the token "MDProjectConfig"
  inputfilestream >> token;
  std::cout << "the first token in Simulation is " << token << std::endl;
  if (token != "MDProjectConfig")
  {
    cerr << "Not a MDProject config file! " << token << endl;
    exit(1);
  }
  while (inputfilestream)
  {
    token.clear();
    inputfilestream >> token;

    if (token.substr(0, 1)=="#")
    {
      inputfilestream.ignore(INT_MAX,'\n');
    } else if (token=="phaseSpaceFile")
    {
      inputfilestream >> phaseSpaceFileName;
      std::cout << "phaseSpaceFileName: " << inputPath + phaseSpaceFileName << std::endl;
      _domain->setPhaseSpaceFile(inputPath + phaseSpaceFileName);
      _domain->readPhaseSpaceHeader();
      _domain->initParameterStreams(_cutoffRadius);
    } else if (token=="timestepLength")
    {
      inputfilestream >> timestepLength;
    } else if (token=="cutoffRadius")
    {
      inputfilestream >> _cutoffRadius;
    } else if (token=="datastructure")
    {
      inputfilestream >> token;
      if (token=="LinkedCells")
      {

        int cellsInCutoffRadius;
        inputfilestream >> cellsInCutoffRadius;
        double bBoxMin[3];
        double bBoxMax[3];
        for (int i=0; i<3; i++)
        {
          bBoxMin[i] = _domainDecomposition->getCoords(i)
              *_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
          bBoxMax[i] = (_domainDecomposition->getCoords(i)+1)
              *_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
        }

        _moleculeContainer = new datastructures::LinkedCells<Molecule>(bBoxMin, bBoxMax,
            _cutoffRadius, cellsInCutoffRadius, *_particlePairsHandler);
      } else if (token=="AdaptiveSubCells")
      {
        int cellsInCutoffRadius;
        inputfilestream >> cellsInCutoffRadius;
        double bBoxMin[3];
        double bBoxMax[3];
        for (int i=0; i<3; i++)
        {
          bBoxMin[i] = _domainDecomposition->getCoords(i)
              *_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
          bBoxMax[i] = (_domainDecomposition->getCoords(i)+1)
              *_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
        }
        //creates a new Adaptive SubCells datastructure
        _moleculeContainer = new datastructures::AdaptiveSubCells<Molecule>(bBoxMin, bBoxMax,
            _cutoffRadius, cellsInCutoffRadius, *_particlePairsHandler);
      }
    } else if (token=="output")
    {
      inputfilestream >> token;
      if (token=="ResultWriter")
      {
        string outputPathAndPrefix;
        inputfilestream >> outputPathAndPrefix;
        _outputPlugins.push_back(new md_io::ResultWriter(outputPathAndPrefix));
      } else if (token=="XyzWriter")
      {
        unsigned long writeFrequency;
        string outputPathAndPrefix;
        inputfilestream >> writeFrequency >> outputPathAndPrefix;
        _outputPlugins.push_back(new md_io::XyzWriter(writeFrequency, outputPathAndPrefix, _numberOfTimesteps, true));
      }
    }
  }
  
  
  _domain->readPhaseSpaceData(_moleculeContainer);
  _domain->initFarFieldCorr(_cutoffRadius);
#else
  if (ownrank == 0)
  {
    cout << "\n------------------------------------------------------------\n";
    cout << "LS1 (MarDyn)\n\n";
  }
#ifndef BOOST_PROGRAM_OPTIONS_VERSION_HPP_VP_2004_04_05
  bool _cl_version, _cl_help, _cl_incremental;
  _cl_help = _cl_version = _cl_incremental = false;
  string _cl_input_file = "";
  string _cl_output = "ckp";
  string _cl_output_filename = "default";
  int _cl_timesteps = 0;
  int _cl_output_frequency = 100;

  int arg_c = 1;
  while(arg_c < *argc)
  {
    if (((string)(*argv)[arg_c] == "-h")||((string)(*argv)[arg_c] == "--help"))
      _cl_help = true;
    else if (((string)(*argv)[arg_c] == "-v")||((string)(*argv)[arg_c] == "--version"))
      _cl_version = true;
    else if (((string)(*argv)[arg_c] == "-t")||((string)(*argv)[arg_c] == "--timesteps"))
    {
      arg_c++;
      _cl_timesteps = atoi((*argv)[arg_c]);
    }
    else if (((string)(*argv)[arg_c] == "-f")||((string)(*argv)[arg_c] == "--output-frequency"))
    {
      arg_c++;
      _cl_output_frequency = atoi((*argv)[arg_c]);
    }
    else if (((string)(*argv)[arg_c] == "-p")||((string)(*argv)[arg_c] == "--output-filename"))
    {
      arg_c++;
      _cl_output_filename = (string)(*argv)[arg_c];
    }
    else if (((string)(*argv)[arg_c] == "-o")||((string)(*argv)[arg_c] == "--output"))
    {
      arg_c++;
      _cl_output = (string)(*argv)[arg_c];
    }
    else if (((string)(*argv)[arg_c] == "-i")||((string)(*argv)[arg_c] == "--incremental"))
      _cl_incremental = true;
    else if ((*argv)[arg_c][0] != '-')
      _cl_input_file = (*argv)[arg_c];

    arg_c++;
  }
  if ((*argc < 4)||(_cl_help))
  {
    if (ownrank == 0)
    {
      cout << "Syntax: " << *argv[0]
           << " [options] -t <timesteps> <input file>" << endl << endl
	   << "Options:" << endl
	   << "  -o [ --output ] arg (=ckp)              comma seperated list of output"
	   << endl
	   << "                                          formats; can be one or more of"
	   << endl
	   << "                                          {pov,vis,res,ckp,xyz}; default is"
	   << endl
	   << "                                          ckp."
	   << endl
	   << "  -t [ --timesteps ] arg                  Number of timesteps to simulate."
	   << endl
	   << "  -f [ --output-frequency ] arg (=100)    output frequency, default is each 100"
	   << endl
	   << "                                          steps."
	   << endl
	   << "  -p [ --output-filename ] arg (=default) filename chosen for output files, the"
	   << endl
	   << "                                          default is 'yyyy-mm-dd_hh-mm-ss_out'."
	   << endl
	   << "  -i [ --incremental ]                    don't overwrite periodical output"
	   << endl
	   << "                                          files."
	   << endl
	   << "  -v [ --version ]      prints version string."
	   << endl
	   << "  -h [ --help ]         show this help message."
	   << endl;
    }
    delete _domainDecomposition;
    if (_cl_help)
      exit(0);
    else
      exit(1);
  }

  if (ownrank == 0)
  {
    cout << " +----------------------------------------------------------"
         << endl
         << " | input-file          : "
         << _cl_input_file << endl;
    cout << " | output filename     : "
         << _cl_output_filename << endl;
    cout << " | number of timesteps : "
         << _cl_timesteps << endl;
    cout << " | output frequency    : each "
         << _cl_output_frequency << " steps";
    if (_cl_incremental)
      cout << " (incremental)";
    cout << endl;
    cout << " | output format       : "
         << _cl_output << endl;
    cout << " +----------------------------------------------------------"
         << endl << endl;

  }
  
  string inputPath;
  int lastIndex = _cl_input_file.find_last_of('/',_cl_input_file.size()-1);
  if (lastIndex == string::npos)
    inputPath="";
  else 
    inputPath = _cl_input_file.substr(0, lastIndex+1);

  // store number of timesteps to be simulated
  _numberOfTimesteps = _cl_timesteps;

  // store prefix for output files
  _outputPrefix = _cl_output_filename;

  // store incremental flag
  if (_cl_incremental)
    _increment = true;
  else
    _increment = false;
  
  _domain = new Domain(ownrank);
  _particlePairsHandler = new datastructures::ParticlePairs2PotForceAdapter(*_domain);

  // prepare XML-config parsing
  md_io::XMLReader_main * _xmlreader = new md_io::XMLReader_main();
  
  TiXmlDocument XMLdoc;
  TiXmlDocument *XMLdoc_p;
  char *xmldoc_ca;
  int len;
  if (ownrank == 0)
  {
    XMLdoc = _xmlreader->XMLReader_main_get_doc(_cl_input_file);
    XMLdoc_p = &XMLdoc;

    // merge out-sourced XML code into the current tree
    string xmldoc_s;
    xmldoc_s = _xmlreader->merge(XMLdoc_p, inputPath);
    len = strlen(xmldoc_s.c_str());
    xmldoc_ca = (char *) malloc(len + 1);
    strcpy(xmldoc_ca,xmldoc_s.c_str());
  }

  // re-read the XML document
#ifdef PARALLEL
  if (ownrank == 0)
  {
    MPI_Bcast (&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast (xmldoc_ca, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    XMLdoc = _xmlreader->XMLReader_main_get_doc_chara(xmldoc_ca);
  } else
  {
    MPI_Bcast (&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
    xmldoc_ca = (char *) malloc (len);
    MPI_Bcast (xmldoc_ca, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    XMLdoc = _xmlreader->XMLReader_main_get_doc_chara(xmldoc_ca);
    XMLdoc_p = &XMLdoc;
  }
  MPI_Barrier(MPI_COMM_WORLD);
#else
  XMLdoc = _xmlreader->XMLReader_main_get_doc_chara(xmldoc_ca);
#endif

  // sanity check
  if (ownrank == 0)
  {
    if (_xmlreader->Eval_i(XMLdoc_p,
            (string)"/mardyncfg/header/version/text()") < 20070725)
    {
      cout << "Error parsing config file: version too old!" << endl;
      exit(1);
    }
    std::cout << "sanity check ok" << std::endl;
  }

  // retrieve timestepLength
  double timestepLength = _xmlreader->Eval_d(XMLdoc_p,
      (string)"/mardyncfg/experiment/timestep-length/text()");
  if (timestepLength == 0.0)
  {
    cout << "Error parsing config file: empty timestep-length value!" << endl;
    exit(1);
  }

  // retrieve the cutoff radius
  _cutoffRadius = _xmlreader->Eval_d(XMLdoc_p,
      (string)"/mardyncfg/experiment/cutoff-radius/text()");
  if (_cutoffRadius == 0.0)
  {
    cout << "Error parsing config file: empty cutoff-radius value!" << endl;
    exit(1);
  }

  // retrieve phase-space
  string phaseSpaceFileName = _xmlreader->Eval_str(XMLdoc_p,
      (string)"/mardyncfg/experiment/phase-space/@source");
  if (phaseSpaceFileName == "")
  {
    cout << "Error parsing config file: empty phase space filename!" << endl;
    exit(1);
  }
  if (ownrank == 0)
  {
    phaseSpaceFileName = inputPath + phaseSpaceFileName;
    ifstream dummy;
    dummy.open(phaseSpaceFileName.c_str(), ifstream::in);

    dummy.close();
    if (dummy.fail())
    {
      cout << "Error parsing config file: space filename "
      << phaseSpaceFileName << " does not exist!" << endl;
      exit(1);
    }
  }

  // retrieve phase-space format
  string phaseSpaceFormat = _xmlreader->Eval_str(XMLdoc_p,
      (string)"/mardyncfg/experiment/phase-space/@format");

  // retrieve components file
  string phaseSpaceHeaderFile = _xmlreader->Eval_str(XMLdoc_p,
      (string)"/mardyncfg/experiment/components/@source");

  // retrieve components format
  string phaseSpaceHeaderFormat = _xmlreader->Eval_str(XMLdoc_p,
      (string)"/mardyncfg/experiment/components/@format");

  // invoke reader modules
  md_io::AsciiReader * _AsciiReader = new md_io::AsciiReader();
  md_io::XMLReader * _XMLReader = new md_io::XMLReader();

  // decide which phase space readers to activate
  if (phaseSpaceFormat == "ASCII")
  {
#ifdef PARALLEL
    char *inp_ca;
    int len;
    if (ownrank == 0) {
       ifstream inp_stream;
       inp_stream.open(phaseSpaceFileName.c_str());
       string inp;
       getline(inp_stream, inp, (char)EOF);
       len = strlen(inp.c_str());
       inp_ca = (char *) malloc(len + 1);
       strcpy(inp_ca,inp.c_str());

       MPI_Bcast (&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
       MPI_Bcast (inp_ca, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
       _AsciiReader->setPhaseSpaceFile(inp);
    } else {
       MPI_Bcast (&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
       inp_ca = (char *) malloc (len);
       MPI_Bcast (inp_ca, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
       string inp(inp_ca);
       _AsciiReader->setPhaseSpaceFile(inp);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    free(inp_ca);
#else
    _AsciiReader->setPhaseSpaceFile(phaseSpaceFileName);
#endif
  } else if (phaseSpaceFormat == "XML")
  {
    cout << "Error parsing config file: the XMLReader module doesn't contain a phase space parser yet." << endl;
    exit(1);
  } else
  {
    cout << "Error parsing config file: invalid phase space format: "
    << phaseSpaceFormat << endl;
    exit(1);
  }
  // decide which component reader to activate
  //
  // ASCII-internal: default .inp file format, usually paired with the
  //                 phase space in the same file, all in ASCII format.
  // ASCII-external: like .inp file format but with phase space and
  //                 components in seperate files.
  // XML-internal: components are in XML format included directly in
  //               the simulation description.
  // XML-external: components are described in a seperate XML file.
  if (phaseSpaceHeaderFormat == "ASCII-internal")
  {
    _AsciiReader->setPhaseSpaceHeaderFile(phaseSpaceHeaderFile);
    _AsciiReader->readPhaseSpaceHeader(_domain);
  } else if (phaseSpaceHeaderFormat == "ASCII-external")
  {
    _AsciiReader->setPhaseSpaceHeaderFile(phaseSpaceHeaderFile);
    _AsciiReader->readPhaseSpaceHeader(_domain);
    // If the components (historically called header) aren't in ASCII
    // format we can assume that they're either included in the config
    // file or pointed to via an external XML file. Either way, as all 
    // external files were merged above, the reader expects a unified file
    // containing all simulation related information provided by '_temp.xml'.
    // This permits the user to outsource any piece of the configuration
    // file, e.g. single components.
  } else
  {
    _XMLReader->setPhaseSpaceHeaderFile(xmldoc_ca);
    _XMLReader->readPhaseSpaceHeader(_domain);
  }
  free(xmldoc_ca);

  _domain->initParameterStreams(_cutoffRadius);

  // retrieve and process data structure information
  int cellsInCutoffRadius;
  if (_xmlreader->Eval_str(XMLdoc_p, (string)"/mardyncfg/experiment/data-structure/*[name()='linked-cells']") == "linked-cells")
  {
    cellsInCutoffRadius = _xmlreader->Eval_i(XMLdoc_p,
        (string)"/mardyncfg/experiment/data-structure/linked-cells/text()");
    double bBoxMin[3];
    double bBoxMax[3];
    for(int i=0;i<3;i++)
    {
      bBoxMin[i] = _domainDecomposition->getCoords(i)*_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
      bBoxMax[i] = (_domainDecomposition->getCoords(i)+1)*_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
    }

    _moleculeContainer = new datastructures::LinkedCells<Molecule>(bBoxMin,
        bBoxMax, _cutoffRadius, cellsInCutoffRadius,
        *_particlePairsHandler);
  }
  else if (_xmlreader->Eval_str(XMLdoc_p, (string)"/mardyncfg/experiment/data-structure/*[name()='adaptiveSubCells']") == "adaptiveSubCells")
  {
    int cellsInCutoffRadius;
    cellsInCutoffRadius = _xmlreader->Eval_i(XMLdoc_p,
        (string)"/mardyncfg/experiment/data-structure/adaptiveSubCells/text()");

    double bBoxMin[3];
    double bBoxMax[3];
    for(int i=0;i<3;i++)
    {
      bBoxMin[i] = _domainDecomposition->getCoords(i)*_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
      bBoxMax[i] = (_domainDecomposition->getCoords(i)+1)*_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
    }
    //creates a new Adaptive SubCells datastructure
    _moleculeContainer = new datastructures::AdaptiveSubCells<Molecule>(bBoxMin, bBoxMax,
        _cutoffRadius, cellsInCutoffRadius, *_particlePairsHandler);

  }
  else
  {
    cout << "Error parsing config file: no valid data structure found!" <<
    endl;
    cout << "Cannot associate '" << _xmlreader->Eval_str(XMLdoc_p, (string)"/mardyncfg/experiment/data-structure/*[name()='a']") <<
    "' with a data structure." << endl;
    exit(1);
  }

  if (phaseSpaceFormat == "ASCII")
  {
    _AsciiReader->readPhaseSpace(_moleculeContainer, _domain);
    _domain->initFarFieldCorr(_cutoffRadius);
  }

  _domain->initFarFieldCorr(_cutoffRadius);

  // handle output modules
  string::size_type loc;

  loc = _cl_output.find("ckp",0);
  if( loc != string::npos )
  {
    _outputPlugins.push_back(new md_io::CheckpointWriter(_outputFrequency, _outputPrefix, _numberOfTimesteps, _increment));
  }
  loc = _cl_output.find("vis",0);
  if( loc != string::npos )
  {
    _outputPlugins.push_back(new md_io::VISWriter(_outputFrequency, _outputPrefix, _numberOfTimesteps, _increment));
  }
  loc = _cl_output.find("pov",0);
  if( loc != string::npos )
  {
    _outputPlugins.push_back(new md_io::PovWriter(_outputFrequency, _outputPrefix, _numberOfTimesteps, _increment));
  }
  loc = _cl_output.find("res",0);
  if( loc != string::npos )
  {
    _outputPlugins.push_back(new md_io::ResultWriter(_outputPrefix));
  }
  loc = _cl_output.find("xyz",0);
  if( loc != string::npos )
  {
    _outputPlugins.push_back(new md_io::XyzWriter(_outputFrequency, _outputPrefix, _numberOfTimesteps, _increment));
  }

#else

  po::options_description cl("Command line only options");
  cl.add_options()
  ("version,v", "prints version string.")
  ("help,h", "show this help message.")
  ;
  po::options_description generic("Configuration values");
  generic.add_options()
  ("output,o",
      po::value<string>()->default_value("ckp"),
      "comma seperated list of output formats; can be one or more of {pov,vis,res,ckp,xyz}; default is ckp.")
  ("timesteps,t",
      po::value<long>(),
      "Number of timesteps to simulate.")
  ("output-frequency,f",
      po::value<long>()->default_value(100),
      "output frequency, default is each 100 steps.")
  ("output-filename,p",
      po::value<string>()->default_value("default"),
      "filename chosen for output files, the default is 'yyyy-mm-dd_hh-mm-ss_out'.")
  ("incremental,i",
      "don't overwrite periodical output files.")
  ;

  po::options_description hidden("Hidden Options");
  hidden.add_options()
  ("input-file", po::value<string>(),
      "input file")
  ;

  po::options_description cmdline_options;
  cmdline_options.add(cl).add(generic).add(hidden);

  po::options_description cfg_file_options;
  cfg_file_options.add(generic).add(hidden);

  po::options_description visible_options("Syntax: MDProject [options] <input file>\n\nOptions");
  visible_options.add(generic).add(cl);

  po::positional_options_description p;
  p.add("input-file", -1);
  po::variables_map vm;
  store(po::command_line_parser(* argc, * argv).
      options(cmdline_options).positional(p).run(), vm);
  ifstream ifs("ls1.cfg");
  store(parse_config_file(ifs, cfg_file_options), vm);
  notify(vm);

  // handle help request
  if (ownrank == 0)
  {
    if (vm.count("help"))
    {
      cout << visible_options << "\n";
      delete _domainDecomposition;
      exit(1);
    }

    // sanity check for program options
    if (vm.count("input-file"))
    {
      cout << " +----------------------------------------------------------\n"
      << " | input-file          : "
      << vm["input-file"].as<string>() << "\n";
    } else
    {
      cout << "error: input-file not set!\n\n";
      cout << visible_options << "\n";
      delete _domainDecomposition;
      exit(1);
    }
    if (vm.count("output-filename"))
    {
      cout << " | output filename     : "
      << vm["output-filename"].as<string>() << "\n";
    }
    if (vm.count("timesteps"))
    {
      cout << " | number of timesteps : "
      << vm["timesteps"].as<long>() << "\n";
    } else
    {
      cout << "error: no timesteps given, nothing to do!\n\n"
      << visible_options << "\n";
      delete _domainDecomposition;
      exit(1);
    }
    if (vm.count("output-frequency"))
    {
      cout << " | output frequency    : each "
      << vm["output-frequency"].as<long>() << " steps";
      if (vm.count("incremental"))
      {
        cout << " (incremental)";
      }
      cout << "\n";
    }
    if (vm.count("output"))
    {
      cout << " | output format       : "
      << vm["output"].as<string>() << "\n";
    }

    cout << " +----------------------------------------------------------\n\n";

  }

//  ifstream inputfilestream(vm["input-file"].as<string>().c_str());
  string inputFile = vm["input-file"].as<string>();
  string inputPath;
  int lastIndex = inputFile.find_last_of('/',inputFile.size()-1);
  if (lastIndex == string::npos)
    inputPath="";
  else
    inputPath = inputFile.substr(0, lastIndex+1);

  // store number of timesteps to be simulated
  _numberOfTimesteps = vm["timesteps"].as<long>();

  // store prefix for output files
  _outputPrefix = vm["output-filename"].as<string>();

  // store output frequency for checkpoint files
  _outputFrequency = vm["output-frequency"].as<long>();

  // store incremental flag
  if (vm.count("incremental"))
    _increment = true;
  else
    _increment = false;

  _domain = new Domain(ownrank);
  _particlePairsHandler = new datastructures::ParticlePairs2PotForceAdapter(*_domain);

  // prepare XML-config parsing
  md_io::XMLReader_main * _xmlreader = new md_io::XMLReader_main();
  
  TiXmlDocument XMLdoc;
  TiXmlDocument *XMLdoc_p;
  char *xmldoc_ca;
  int len;
  if (ownrank == 0)
  {
    XMLdoc = _xmlreader->XMLReader_main_get_doc(vm["input-file"].as<string>());
    XMLdoc_p = &XMLdoc;

    // merge out-sourced XML code into the current tree
    string xmldoc_s;
    xmldoc_s = _xmlreader->merge(XMLdoc_p, inputPath);
    len = strlen(xmldoc_s.c_str());
    xmldoc_ca = (char *) malloc(len + 1);
    strcpy(xmldoc_ca,xmldoc_s.c_str());
  }

  // re-read the XML document
#ifdef PARALLEL
  if (ownrank == 0)
  {
    MPI_Bcast (&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast (xmldoc_ca, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    XMLdoc = _xmlreader->XMLReader_main_get_doc_chara(xmldoc_ca);
  } else
  {
    MPI_Bcast (&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
    xmldoc_ca = (char *) malloc (len);
    MPI_Bcast (xmldoc_ca, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    XMLdoc = _xmlreader->XMLReader_main_get_doc_chara(xmldoc_ca);
    XMLdoc_p = &XMLdoc;
  }
  MPI_Barrier(MPI_COMM_WORLD);
#else
  XMLdoc = _xmlreader->XMLReader_main_get_doc_chara(xmldoc_ca);
#endif

  // sanity check only needs to be done once
  if (ownrank == 0)
  {
    if (_xmlreader->Eval_i(XMLdoc_p,
            (string)"/mardyncfg/header/version/text()") < 20070725)
    {
      cout << "Error parsing config file: version too old!" << endl;
      exit(1);
    }
    std::cout << "sanity check ok" << std::endl;
  }

  // retrieve timestepLength
  double timestepLength = _xmlreader->Eval_d(XMLdoc_p,
      (string)"/mardyncfg/experiment/timestep-length/text()");
  if (timestepLength == 0.0)
  {
    cout << "Error parsing config file: empty timestep-length value!" << endl;
    exit(1);
  }

  // retrieve the cutoff radius
  _cutoffRadius = _xmlreader->Eval_d(XMLdoc_p,
      (string)"/mardyncfg/experiment/cutoff-radius/text()");
  if (_cutoffRadius == 0.0)
  {
    cout << "Error parsing config file: empty cutoff-radius value!" << endl;
    exit(1);
  }

  // retrieve phase-space
  string phaseSpaceFileName = _xmlreader->Eval_str(XMLdoc_p,
      (string)"/mardyncfg/experiment/phase-space/@source");
  if (phaseSpaceFileName == "")
  {
    cout << "Error parsing config file: empty phase space filename!" << endl;
    exit(1);
  }
  if (ownrank == 0)
  {	 
    phaseSpaceFileName = inputPath + phaseSpaceFileName;
    ifstream dummy;
    dummy.open(phaseSpaceFileName.c_str(), ifstream::in);

    dummy.close();
    if (dummy.fail())
    {
      cout << "Error parsing config file: space filename "
      << phaseSpaceFileName << " does not exist!" << endl;
      exit(1);
    }
  }

  // retrieve phase-space format
  string phaseSpaceFormat = _xmlreader->Eval_str(XMLdoc_p,
      (string)"/mardyncfg/experiment/phase-space/@format");

  // retrieve components file
  string phaseSpaceHeaderFile = _xmlreader->Eval_str(XMLdoc_p,
      (string)"/mardyncfg/experiment/components/@source");

  // retrieve components format
  string phaseSpaceHeaderFormat = _xmlreader->Eval_str(XMLdoc_p,
      (string)"/mardyncfg/experiment/components/@format");

  // invoke reader modules
  md_io::AsciiReader * _AsciiReader = new md_io::AsciiReader();
  md_io::XMLReader * _XMLReader = new md_io::XMLReader();

  // decide which phase space readers to activate
  if (phaseSpaceFormat == "ASCII")
  {
#ifdef PARALLEL
    char *inp_ca;
    int len;
    if (ownrank == 0) {
       ifstream inp_stream;
       inp_stream.open(phaseSpaceFileName.c_str());
       string inp;
       getline(inp_stream, inp, (char)EOF);
       len = strlen(inp.c_str());
       inp_ca = (char *) malloc(len + 1);
       strcpy(inp_ca,inp.c_str());

       MPI_Bcast (&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
       MPI_Bcast (inp_ca, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
       _AsciiReader->setPhaseSpaceFile(inp);
    } else {
       MPI_Bcast (&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
       inp_ca = (char *) malloc (len);
       MPI_Bcast (inp_ca, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
       string inp(inp_ca);
       _AsciiReader->setPhaseSpaceFile(inp);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    free(inp_ca);
#else
    _AsciiReader->setPhaseSpaceFile(phaseSpaceFileName);
#endif
  } else if (phaseSpaceFormat == "XML")
  {
    cout << "Error parsing config file: the XMLReader module doesn't contain a phase space parser yet." << endl;
    exit(1);
  } else
  {
    cout << "Error parsing config file: invalid phase space format: "
    << phaseSpaceFormat << endl;
    exit(1);
  }
  // decide which component reader to activate
  //
  // ASCII-internal: default .inp file format, usually paired with the
  //                 phase space in the same file, all in ASCII format.
  // ASCII-external: like .inp file format but with phase space and
  //                 components in seperate files.
  // XML-internal: components are in XML format included directly in
  //               the simulation description.
  // XML-external: components are described in a seperate XML file.
  if (phaseSpaceHeaderFormat == "ASCII-internal")
  {
    _AsciiReader->setPhaseSpaceHeaderFile(phaseSpaceHeaderFile);
    _AsciiReader->readPhaseSpaceHeader(_domain);
  } else if (phaseSpaceHeaderFormat == "ASCII-external")
  {
    _AsciiReader->setPhaseSpaceHeaderFile(phaseSpaceHeaderFile);
    _AsciiReader->readPhaseSpaceHeader(_domain);
    // If the components (historically called header) aren't in ASCII
    // format we can assume that they're either included in the config
    // file or pointed to via an external XML file. Either way, as all 
    // external files were merged above, the reader expects a unified file
    // containing all simulation related information provided by '_temp.xml'.
    // This permits the user to outsource any piece of the configuration
    // file, e.g. single components.
  } else
  {
    _XMLReader->setPhaseSpaceHeaderFile(xmldoc_ca);
    _XMLReader->readPhaseSpaceHeader(_domain);
  }
  free(xmldoc_ca);

  _domain->initParameterStreams(_cutoffRadius);

  // retrieve and process data structure information
  int cellsInCutoffRadius;
  if (_xmlreader->Eval_str(XMLdoc_p, (string)"/mardyncfg/experiment/data-structure/*[name()='linked-cells']") == "linked-cells")
  {
    cellsInCutoffRadius = _xmlreader->Eval_i(XMLdoc_p,
        (string)"/mardyncfg/experiment/data-structure/linked-cells/text()");
    double bBoxMin[3];
    double bBoxMax[3];
    for(int i=0;i<3;i++)
    {
      bBoxMin[i] = _domainDecomposition->getCoords(i)*_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
      bBoxMax[i] = (_domainDecomposition->getCoords(i)+1)*_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
    }

    _moleculeContainer = new datastructures::LinkedCells<Molecule>(bBoxMin,
        bBoxMax, _cutoffRadius, cellsInCutoffRadius,
        *_particlePairsHandler);
  }
  else if (_xmlreader->Eval_str(XMLdoc_p, (string)"/mardyncfg/experiment/data-structure/*[name()='adaptiveSubCells']") == "adaptiveSubCells")
  {
    int cellsInCutoffRadius;
    cellsInCutoffRadius = _xmlreader->Eval_i(XMLdoc_p,
        (string)"/mardyncfg/experiment/data-structure/adaptiveSubCells/text()");

    //inputfilestream >> cellsInCutoffRadius;
    double bBoxMin[3];
    double bBoxMax[3];
    for(int i=0;i<3;i++)
    {
      bBoxMin[i] = _domainDecomposition->getCoords(i)*_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
      bBoxMax[i] = (_domainDecomposition->getCoords(i)+1)*_domain->getGlobalLength(i)/_domainDecomposition->getGridSize(i);
    }
    //creates a new Adaptive SubCells datastructure
    _moleculeContainer = new datastructures::AdaptiveSubCells<Molecule>(bBoxMin, bBoxMax,
        _cutoffRadius, cellsInCutoffRadius, *_particlePairsHandler);

  }
  else
  {
    cout << "Error parsing config file: no valid data structure found!" <<
    endl;
    cout << "Cannot associate '" << _xmlreader->Eval_str(XMLdoc_p, (string)"/mardyncfg/experiment/data-structure/*[name()='a']") <<
    "' with a data structure." << endl;
    exit(1);
  }

  if (phaseSpaceFormat == "ASCII")
  {
    _AsciiReader->readPhaseSpace(_moleculeContainer, _domain);
    _domain->initFarFieldCorr(_cutoffRadius);
  }

  _domain->initFarFieldCorr(_cutoffRadius);

  // handle output modules
  string::size_type loc;

  loc = vm["output"].as<string>().find("ckp",0);
  if( loc != string::npos )
  {
    _outputPlugins.push_back(new md_io::CheckpointWriter(_outputFrequency, _outputPrefix, _numberOfTimesteps, _increment));
  }
  loc = vm["output"].as<string>().find("vis",0);
  if( loc != string::npos )
  {
    _outputPlugins.push_back(new md_io::VISWriter(_outputFrequency, _outputPrefix, _numberOfTimesteps, _increment));
  }
  loc = vm["output"].as<string>().find("pov",0);
  if( loc != string::npos )
  {
    _outputPlugins.push_back(new md_io::PovWriter(_outputFrequency, _outputPrefix, _numberOfTimesteps, _increment));
  }
  loc = vm["output"].as<string>().find("res",0);
  if( loc != string::npos )
  {
    _outputPlugins.push_back(new md_io::ResultWriter(_outputPrefix));
  }
  loc = vm["output"].as<string>().find("xyz",0);
  if( loc != string::npos )
  {
    _outputPlugins.push_back(new md_io::XyzWriter(_outputFrequency, _outputPrefix, _numberOfTimesteps, _increment));
  }
#endif
#endif

  // @todo comment
  _integrator = new integrators::Leapfrog(timestepLength);

}

void Simulation::initialize()
{

  // clear halo
  _moleculeContainer->deleteOuterParticles();

  updateParticleContainerAndDecomposition();

  // Force calculation
  _moleculeContainer->traversePairs();

  // clear halo
  _moleculeContainer->deleteOuterParticles();

  //! @todo calculation of macroscopic values, so that the output in
  //!       step 1 is correct. This doesn't work yet as for the methode
  //!       _domain->calculateGlobalValues(...), the iterator has
  //!       to be executed before (sets summv2 and sumIw2)
  _domain->calculateVelocitySums(_moleculeContainer);

  _domain->calculateGlobalValues(_domainDecomposition, _moleculeContainer);

  // initialize output
  std::list<md_io::OutputBase*>::iterator outputIter;
  for (outputIter = _outputPlugins.begin(); outputIter != _outputPlugins.end(); outputIter++)
  {
    (*outputIter)->initOutput(_moleculeContainer, _domainDecomposition,
        _domain);
  }

}

void Simulation::simulate()
{

  Molecule* tempMolecule;
  int ownrank = _domainDecomposition->getRank();
  if (ownrank==0)
    _log.info("simulation(...)", "Starting Simulation: ");
  //double runclock = clock();

  initialize();
  // MAIN LOOP
  for (unsigned long simstep=1; simstep<=_numberOfTimesteps; simstep++)
  {

    _integrator->eventNewTimestep(_moleculeContainer, _domain);

    updateParticleContainerAndDecomposition();

    // Force calculation
    _moleculeContainer->traversePairs();

    // clear halo
    _moleculeContainer->deleteOuterParticles();

    // Inform the integrator about the calculated forces
    _integrator->eventForcesCalculated(_moleculeContainer, _domain);

    // calculate the global macroscopic values from the local values
    _domain->calculateGlobalValues(_domainDecomposition, _moleculeContainer);

    // scale velocity and angular momentum
    // @todo why here? what about preF
    for (tempMolecule = _moleculeContainer->begin(); tempMolecule
        != _moleculeContainer->end(); tempMolecule = _moleculeContainer->next())
    {
      tempMolecule->scale_v(_domain->getGlobalBetaTrans());
      tempMolecule->scale_D(_domain->getGlobalBetaRot());
    }

    _domain->advanceTime(_integrator->getTimestepLength());

    output(simstep);
  }

  string cpfile(_outputPrefix+".restart.inp");
  _domain->writeCheckpoint(cpfile, _moleculeContainer, _domainDecomposition);

  // finish output
  std::list<md_io::OutputBase*>::iterator outputIter;
  for (outputIter = _outputPlugins.begin(); outputIter != _outputPlugins.end(); outputIter++)
  {
    (*outputIter)->finishOutput(_moleculeContainer, _domainDecomposition,
        _domain);
    delete (*outputIter);
  }

  delete _domainDecomposition;
  delete _domain;
  delete _particlePairsHandler;
  delete _moleculeContainer;
  delete _integrator;
  // wait for all processes to reach the end of the program
  //_DomainDecomposition->barrier();
}

void Simulation::output(int simstep)
{
  int ownrank = _domainDecomposition->getRank();

  std::list<md_io::OutputBase*>::iterator outputIter;
  for (outputIter = _outputPlugins.begin(); outputIter != _outputPlugins.end(); outputIter++)
  {
    (*outputIter)->doOutput(_moleculeContainer, _domainDecomposition,
        _domain, simstep);
  }

  if (ownrank==0)
    cout << simstep << "\t" << _domain->getAverageGlobalUpot() << "\t"
        << _domain->getGlobalPressure() << "\t" << endl; //double(runclock)/CLOCKS_PER_SEC << " s" << endl;

}

void Simulation::updateParticleContainerAndDecomposition()
{

  _domainDecomposition->exchangeMolecules(_moleculeContainer,
      _domain->getComponents(), _domain);

  // The cache of the molecules must be updated/build after the exchange process,
  // as the cache itself isn't transferred
  Molecule* tempMolecule;
  for (tempMolecule = _moleculeContainer->begin(); tempMolecule
      != _moleculeContainer->end(); tempMolecule = _moleculeContainer->next())
  {
    tempMolecule->upd_cache();
  }

  // The particles have moved, so the neighbourhood relations have
  // changed and have to be adjusted
  _moleculeContainer->update();

}
