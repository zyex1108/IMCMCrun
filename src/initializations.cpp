#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <iomanip>      // std::setprecision
#include <algorithm>    // std::min_element, std::max_element
#include "rvgs.h"
#include "rngs.h"
#include "defines.h"
#include "configFileParser.h"
#include "initializations.h"
#include "functions.h"
#include "structures.h"
#include "generalFunctions.h"
#include "filesAndControl.h"
#include "wavelet2s.h"

Configuration parameters_setting(int argc, char *argv[])
// Initialize the configuration structure containing the config of the run, read the configuration file
{
  Configuration config; // Create a structure config to store the configuration
  #ifdef PAR // Preprocessor instruction : if PAR is defined <-> if MPI mode (see in define.h)
    config.mpiConfig.rc=MPI_Init( &argc, &argv); // Starting MPI...
    if (config.mpiConfig.rc != MPI_SUCCESS) {    // Check if MPI has been lauch correctly
      std::cout << std::endl << "Error starting MPI program. Terminating" << std::endl;
      MPI_Abort(MPI_COMM_WORLD, config.mpiConfig.rc); // Abort MPI
      exit(0); // Exit program
    }
    // MPI_Status status;
    MPI_Comm_rank( MPI_COMM_WORLD, &config.mpiConfig.rank); // Store rank of the process  
    MPI_Comm_size(MPI_COMM_WORLD, &config.mpiConfig.nb_process); // Store the number of processes
    MPI_Get_processor_name(config.mpiConfig.hostname, &config.mpiConfig.len); // Store processor name
    MPI_Barrier(MPI_COMM_WORLD); // Every process wait for the others here
  #else // If we are not in MPI :
    config.mpiConfig.rank=0; // rank = 0
    config.mpiConfig.nb_process=1; // and just one process working
  #endif
  config.startTime=time(NULL);
  if (config.startTime == -1) { // Sometimes time() returns -1
    std::cout << "TIME PROBLEM"<< std::endl;
    exit(0);
  }
  std::ostringstream startTime;   // Store a part of the starting time to distinguish files from different runs
  startTime << (int)config.startTime%1000; // -> gives 3 digit that will different each time we run the program
  config.code = startTime.str(); // Store these 3 digits on a string : config.code
  char buffer[80];
  convertTime(buffer, config.startTime);
  if (config.mpiConfig.rank == 0) {
    std::cout << "========================================================" << std::endl;
    std::cout << "                 IMPORTANCE RESAMPLING MCMC             " << std::endl;
    std::cout << "========================================================" << std::endl;
    std::cout << "This is the run number "<< config.code << std::endl;
    std::cout << "Starting time : "<< buffer << std::endl<< std::endl;
    #ifdef PAR  
      std::cout << "                  === MPI MODE ===" << std::endl;
      std::cout << config.mpiConfig.nb_process << " processes running on : " << config.mpiConfig.hostname << std::endl;
    #endif
    std::cout << "******************** INITIALIZATION ********************" << std::endl << std::endl;
  }
  std::string nameOfConfFile = NAME_OF_CONFIGURATION_FILE;
  readConfFile(nameOfConfFile,&config); // Read the informations contained in the configuration file
  config.data.nz=-9999; // Will be updated in loadFirstGueses
  config.data.dz=-9999; // Will be updated in loadFirstGueses
  config.outputDir="./OUTPUT_FILES/"+config.code+"/";
  std::string mkdir_command="mkdir -p "+config.outputDir;
  int status;
  if (config.mpiConfig.rank == 0) {
    status = system(mkdir_command.c_str()); // TODO : This is a little bit dirty but there is no simple equivalent to "mkdir -p"
    if(status != 0) {
      std::cout << "Error while creating "+config.outputDir+" (status = "<< status << ")" << std::endl;
      exit(1);
    }
  }
  int seed=config.defaultSeed;
  if (config.useDefaultSeed != 1 && config.mpiConfig.rank == 0)
    seed=time(NULL);
  #ifdef PAR  
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Bcast(&seed,1,MPI_DOUBLE,0,MPI_COMM_WORLD); 
    // The process 0 diffuses the seed to every other process
  #endif
  config.seed=seed;
  PlantSeeds(config.seed); // Seed initialization
  srand(config.seed);      // Seed initialization
  build_temperature_ladders(&config); // Build the temperature ladders (T[0]=1)
  if (config.verbose1 && config.mpiConfig.rank == 0)
    std::cout << "Configuration structure initialized" << std::endl;
  return config;
}

void readConfFile(std::string nameOfConfFile, Configuration* config)
// Read the informations contained in the configuration file
{
  configFileParser::data myconfigdata;
  std::ifstream f(nameOfConfFile.c_str()); // Open the configuration file
  f >> myconfigdata; // store the content of the file in a configFileParser::data
  f.close(); // Close the file
  //std::cout << "This is the configuration file cleaned :" << std::endl;
  //std::cout << myconfigdata;
  //std::cout << std::endl;
  configFileParser::data::const_iterator iter;
  std::string tempNC,tempString;
  for (iter = myconfigdata.begin(); iter != myconfigdata.end(); iter++) {
    if (iter->first == "DATA_DIRECTORY")
      config->filesDir = iter->second;
    if (iter->first == "NPU")
      config->npu = atoi(iter->second.c_str());
    if (iter->first == "A_PRIOR")
      config->A = atof(iter->second.c_str());
    if (iter->first == "N_PRIOR_PROFILES")
      config->nPriorProfiles = atoi(iter->second.c_str());
    if (iter->first == "SIGMAP")
      config->data.sigmaP = atof(iter->second.c_str());
    if (iter->first == "SIGMAS")
      config->data.sigmaS = atof(iter->second.c_str());
    if (iter->first == "DI")
      config->di = atof(iter->second.c_str());
    if (iter->first == "DF")
      config->df = atof(iter->second.c_str());
    if (iter->first == "QP")
      config->qp = atof(iter->second.c_str());
    if (iter->first == "NAME_OF_REAL_PROFILE_FILE_P")
      config->name_of_real_profile_P = iter->second;
    if (iter->first == "NAME_OF_REAL_PROFILE_FILE_S")
      config->name_of_real_profile_S = iter->second;
    if (iter->first == "NAME_OF_FIRST_GUESS_P_FILE")
      config->name_of_first_guess_P_file = iter->second;
    if (iter->first == "NAME_OF_FIRST_GUESS_S_FILE")
      config->name_of_first_guess_S_file = iter->second;
    if (iter->first == "NAME_OF_TIMES_FILE")
      config->name_of_times_file = iter->second;
    if (iter->first == "NAME_OF_STATIONS_FILE")
      config->name_of_stations_file = iter->second; 
    if (iter->first == "NAME_OF_SHOTS_FILE")
      config->name_of_shots_file = iter->second;
    if (iter->first == "NAME_OF_PRIOR_FEATURES_FILE")
      config->name_of_prior_features_file = iter->second;       
    if (iter->first == "NSWEEPS")
      config->nSweeps = atoi(iter->second.c_str());
    if (iter->first == "EPSIN")
      config->epsin = atof(iter->second.c_str());
    if (iter->first == "NBT")
      config->nbt = atoi(iter->second.c_str());
    if (iter->first == "TMAX")
      config->tmax = atof(iter->second.c_str()); 
    if (iter->first == "NIT")
      config->nit = atoi(iter->second.c_str()); 
    if (iter->first == "PEE")
      config->pee = atof(iter->second.c_str()); 
    if (iter->first == "NC")
      tempNC = iter->second; 
    if (iter->first == "WAVELET")
      config->wavelet = iter->second;
    if (iter->first == "NDWTS")
      config->ndwts = atoi(iter->second.c_str());
    if (iter->first == "SWAVES")
      config->swaves = atoi(iter->second.c_str()); 
    if (iter->first == "VERBOSE1")
      config->verbose1 = atoi(iter->second.c_str());
    if (iter->first == "VERBOSE2")
      config->verbose2 = atoi(iter->second.c_str());
    if (iter->first == "VIEW")
      config->view = atoi(iter->second.c_str()); 
    if (iter->first == "COORD_TOL")
      config->coordTol = atof(iter->second.c_str());
    if (iter->first == "BUILD_PRIOR")
      config->buildPrior = atoi(iter->second.c_str());
    if (iter->first == "FIND_OPTIMUM_GRID")
      config->findOptimumGrid = atoi(iter->second.c_str());
    if (iter->first == "SHOT_NUMBER_REF")
      config->shotNumberRef = atoi(iter->second.c_str());
    if (iter->first == "NXREF")
      config->nxref = atoi(iter->second.c_str());
    if (iter->first == "NYREF")
      config->nyref = atoi(iter->second.c_str());
    if (iter->first == "NUMBER_OF_X_POINTS_TO_DESCRIBE_THE_SMALLEST_DISTANCE_DEFAULT")
      config->numberOfXpointsToDescribeTheSmallestDistanceDefault = atoi(iter->second.c_str());
    if (iter->first == "NUMBER_OF_Y_POINTS_TO_DESCRIBE_THE_SMALLEST_DISTANCE_DEFAULT")
      config->numberOfYpointsToDescribeTheSmallestDistanceDefault = atoi(iter->second.c_str());
    if (iter->first == "NZFILT_DEFAULT")
      config->nzfiltDefault = atoi(iter->second.c_str());
    if (iter->first == "ANALYTICAL_RUN")
      config->analyticalRun = atoi(iter->second.c_str());
    if (iter->first == "RECALCULATE_T0")
      config->recalculateT0 = atoi(iter->second.c_str());
    if (iter->first == "USE_DEFAULT_SEED")
      config->useDefaultSeed = atoi(iter->second.c_str());
    if (iter->first == "DEFAULT_SEED")
      config->defaultSeed = atoi(iter->second.c_str());
    if (iter->first == "TEST")
      config->test = atoi(iter->second.c_str());
    if (iter->first == "MIN_E_TEST")
      config->minEtest = atof(iter->second.c_str());
    if (iter->first == "MAX_E_TEST")
      config->maxEtest = atof(iter->second.c_str());
    if (iter->first == "N_BEST_PROFILES")
      config->nBestProfiles = atoi(iter->second.c_str());
    if (iter->first == "COMPUTE_RESIDUALS")
      config->computeResiduals = atoi(iter->second.c_str());
    if (iter->first == "ITERATIONS_RESIDUALS")
      config->iterationsResiduals = atoi(iter->second.c_str());
    if (iter->first == "ITERATIONS_BEST_PROFILES")
      config->iterationsBestProfiles = atoi(iter->second.c_str());
    if (iter->first == "NOXPTDTSDVEC") {
      tempString = trim(iter->second);
      int nValGiven = std::count(tempString.begin(), tempString.end(), ',') + 1;
      std::replace(tempString.begin(), tempString.end(), ',', ' ' ); // replace the comas with spaces
      if (!tempString.empty()) {
        std::stringstream sline(tempString);
        for (int i=0; i<nValGiven;i++) {
          double temp;
          sline >> temp;
          config->noXptdtsdVec.push_back(temp);
        }
      }
    }
    if (iter->first == "NOYPTDTSDVEC") {
      tempString = trim(iter->second);
      int nValGiven = std::count(tempString.begin(), tempString.end(), ',') + 1;
      std::replace(tempString.begin(), tempString.end(), ',', ' ' ); // replace the comas with spaces
      if (!tempString.empty()) {
        std::stringstream sline(tempString);
        for (int i=0; i<nValGiven;i++) {
          double temp;
          sline >> temp;
          config->noYptdtsdVec.push_back(temp);
        }
      }
    }
    if (iter->first == "NZFILTVEC") {
      tempString = trim(iter->second);
      int nValGiven = std::count(tempString.begin(), tempString.end(), ',') + 1;
      std::replace(tempString.begin(), tempString.end(), ',', ' ' ); // replace the comas with spaces
      if (!tempString.empty()) {
        std::stringstream sline(tempString);
        for (int i=0; i<nValGiven;i++) {
          double temp;
          sline >> temp;
          config->nzFiltVec.push_back(temp);
        }
      }
    }
  }  
  if (std::count(tempNC.begin(), tempNC.end(), ',') < config->nbt-1) { 
      std::cout << std::count(tempNC.begin(), tempNC.end(), ',')+1 << " values of NC given while " << config->nbt;
      std::cout << " chains will run! " << std::endl;
      std::cout << "Terminating..." << std::endl;
      exit(0);
  }
  int numberOfParameters = 0;
  if(config->swaves)
    numberOfParameters = 2*config->npu;
  else
    numberOfParameters = 2*config->npu;
  tempNC = trim(tempNC);
  std::replace(tempNC.begin(), tempNC.end(), ',', ' ' ); // replace the comas with spaces
  if (!tempNC.empty()) {
    std::stringstream sline(tempNC);
    for (int i=0; i<config->nbt;i++) {
      double temp;
      sline >> temp;
      if(temp > numberOfParameters) {
        std::cout << "Number of component specified (" << temp << ") for NC is too high (this is a run with "<< numberOfParameters;
        std::cout << " parameters)" << std::endl;
        std::cout << "Terminating..." << std::endl;
        exit(0);
      }
      config->nc.push_back(temp);
    }
  }  
  checkConfig(config);  // Check the config
  if (config->verbose1 && config->mpiConfig.rank == 0)
    std::cout << "  Configuration file read" << std::endl;
}

void build_temperature_ladders(Configuration* config)
// Build the temperature ladders (T[0]=1)
{
  double incr=0.;
  config->T.push_back(TMIN);
  incr=pow(config->tmax/config->T[0],1.0/(config->nbt-1));
  for (int i=1;i<config->nbt;i++)
    config->T.push_back(config->T[i-1]*incr);
}

void loadArrivalTimes(Configuration* config)
// Load the first arrival times for P and S waves contained on the file tObs4096.txt
{
  std::string pick(config->filesDir+config->name_of_times_file);
  std::ifstream file(pick.c_str()); // First arrival times P and S
  if(file == NULL) {
    std::cout << "IMPOSSIBLE TO OPEN "+config->filesDir+config->name_of_times_file << std::endl;
    exit(0);
  }
  std::string line;
  while(getline(file, line)) {
    line = trim(line);
    if (!line.empty()) {
      std::stringstream sline(line);
      double tp,ts;
      sline >> tp >> ts;
      config->data.times.timesP.push_back(tp);
      config->data.times.timesS.push_back(ts);
    }
  }
  file.close();
  if (config->verbose1 && config->mpiConfig.rank == 0)
    std::cout << "  Arrival times loaded" << std::endl;
}

void loadStationsPositions(Configuration* config)
// Load the receivers positions contained on the file coordStats.txt
{
  std::string coordStations(config->filesDir+config->name_of_stations_file);
  std::ifstream file(coordStations.c_str()); // First arrival times P and S
  if(file == NULL) {
    std::cout << "IMPOSSIBLE TO OPEN "+config->filesDir+config->name_of_stations_file << std::endl;
    exit(0);
  }
  Coordinate X;
  std::string line;
  while(getline(file, line)) {
    line = trim(line);
    if (!line.empty()) {
      std::stringstream sline(line);
      sline >> X.x >> X.y >> X.z ;
      config->data.coordStations.push_back(X);
    }
  }
  file.close();
  if (config->verbose1 && config->mpiConfig.rank == 0)
    std::cout << "  Receivers positions loaded" << std::endl;
}

void loadShotsPositions(Configuration* config)
// Load the sources positions contained on the file coordShots.txt
{
  std::string coordShots(config->filesDir+config->name_of_shots_file);
  std::ifstream file(coordShots.c_str()); // First arrival times P and S
  if(file == NULL) {
    std::cout << "IMPOSSIBLE TO OPEN "+config->filesDir+config->name_of_shots_file << std::endl;
    exit(0);
  }
  Coordinate X;
  std::string line;
  while(getline(file, line)) {
    line = trim(line);
    if (!line.empty()) {
      std::stringstream sline(line);
      sline >> X.x >> X.y >> X.z ;
      config->data.coordShots.push_back(X);
    }
  }
  file.close();
  
  #ifdef PAR 
    int maxProcNeeded = 0;
    if(config->swaves)
      maxProcNeeded =  2*(int)config->data.coordShots.size();
    else
      maxProcNeeded = (int)config->data.coordShots.size();  
    if (config->mpiConfig.nb_process > maxProcNeeded) {
      if(config->mpiConfig.rank == 0) {
        std::cout << "You are using too much CPUs, " << maxProcNeeded << " is enough !" << std::endl;
        std::cout << "Terminating.." << std::endl;
      }
      exit(0);
    }
  #endif
  
  if (config->verbose1 && config->mpiConfig.rank == 0)
    std::cout << "  Shots positions loaded" << std::endl;
}

void loadPriorFeatures(Configuration* config)
// Load prior features (min, max, amplitude of variation) contained on the file priorFeatures.txt. Calculate prior energy.
{
  std::string priorFeatures(config->filesDir+config->name_of_prior_features_file);
  std::ifstream file(priorFeatures.c_str()); // First arrival times P and S
  if(file == NULL) {
    std::cout << "IMPOSSIBLE TO OPEN "+config->filesDir+config->name_of_prior_features_file << std::endl;
    exit(0);
  }
  std::string line;
  while(getline(file, line)) {
    line = trim(line);
    if (!line.empty()) {
      std::stringstream sline(line);
      int idx;  // The params[i] refer to the wavelet coefficient number idx of the profile
      double mini,maxi;
      sline >> idx >> mini >> maxi ;
      config->data.minParameters.push_back(mini);
      config->data.maxParameters.push_back(maxi);
      config->data.indexParameters.push_back(idx);
    }
  }
  file.close();

  double Ep=1.;
  for(int i=0;i<(int)config->data.minParameters.size();i++) { // Loop on the parameters, to calculate the prior probability and computed the non varying parameters
    if(fabs(config->data.maxParameters[i]-config->data.minParameters[i]) > TINYVAL)
      Ep*=fabs(config->data.maxParameters[i]-config->data.minParameters[i]);
    else
      config->data.staticParameters.push_back(i); // This parameters are static (minParam=maxParam : they are actually not parameters...)
  }
  config->data.Ep=log(Ep);
  if (config->verbose1 && config->mpiConfig.rank == 0)
    std::cout << "  Prior features loaded" << std::endl;
}

void loadFirstGuesses(Configuration* config)
// Load first guesses on the files falseFirstGuessP4096.txt, falseFirstGuessS4096.txt. Filter them and store the results in config. 
// Must be done after loading priorFeatures.txt 
{
  std::string firstGuessP(config->filesDir+config->name_of_first_guess_P_file);
  std::ifstream fileP(firstGuessP.c_str());
  if(fileP == NULL) {
    std::cout << "IMPOSSIBLE TO OPEN "+config->filesDir+config->name_of_first_guess_P_file << std::endl;
    exit(0);
  }
  std::string lineP;
  while(getline(fileP, lineP)) {
    lineP = trim(lineP);
    if (!lineP.empty()) {
      std::stringstream slineP(lineP);
      double zpFirstGuess,value;
      slineP >> zpFirstGuess >> value ;
      config->data.firstGuessP.push_back(value);
      config->data.zpFirstGuess.push_back(zpFirstGuess);
    }
  }

  config->data.zp = config->data.zpFirstGuess; // In fact these two vectors are equal even for an analytical run but we need to declare it.
  if (config->data.zp[1] > config->data.zp[0])
    config->data.dz = config->data.zp[1]-config->data.zp[0];
  else {
    std::cout << "By now z must point downwards. Terminating..." << std::endl;
    exit(0);
  } 
  // Build z : it will contains the z positions of the cells borders
  config->data.z.push_back(config->data.zp[0]-config->data.dz/2.0); // Compute the first point
  if (fabs(config->data.z[0]) < TINYVAL)
    config->data.z[0] = TINYVAL;
  for(int i=1;i<(int)config->data.zp.size()+1;i++)
    config->data.z.push_back(config->data.z[i-1]+config->data.dz);
  fileP.close();
  config->data.nz=config->data.z.size();
  // ******** DISCRETE WAVELET TRANSFORM IMPLEMENTATION*********"
  // Performing Non Linear Approximation by using only config->npu most significant coefficients
  // Coefficients in config->coeffsP (or config->coeffsS) are stored as following
  // config->coeffsP =[Appx(J-1) Detail(J-1) Detail(J-2) .... Detail(0)]
  dwt(config->data.firstGuessP, config->ndwts, config->wavelet, config->coeffsP,config->flagP, config->lengthP); // Performs J-Level DWT
  keepNsignificantValues(&config->coeffsP,config->npu); // Keep the N biggest absolute values in vector. Put the others to 0
  idwt(config->coeffsP,config->flagP,config->wavelet,config->data.filtFirstGuessP,config->lengthP);  // Performs IDWT with approximated coefficients
  if (config->mpiConfig.rank == 0)     // (In case of parallel implementation just one process has to create files)
    write_two_columns_file(&config->data.zp,&config->data.filtFirstGuessP, config->outputDir+"filteredFirstGuessP."+config->code+".dat");
 
  if(config->swaves) {
    std::string firstGuessS(config->filesDir+config->name_of_first_guess_S_file);
    std::ifstream fileS(firstGuessS.c_str());
    if(fileS == NULL) {
      std::cout << "IMPOSSIBLE TO OPEN "+config->filesDir+config->name_of_first_guess_S_file << std::endl;
      exit(0);
    }
    std::string lineS;
    int i=0;
    while(getline(fileS, lineS)) {
      lineS = trim(lineS);
      if (!lineS.empty()) {
        std::stringstream slineS(lineS);
        double zpFirstGuess,value;
        slineS >> zpFirstGuess >> value ;
        if (fabs(zpFirstGuess-config->data.zpFirstGuess[i]) > TINYVAL) {
          std::cout << "Depths read in "+config->filesDir+config->name_of_first_guess_S_file+" are not consistent with those in : ";
          std::cout << config->filesDir+config->name_of_first_guess_P_file+"! Terminating..." << std::endl;
          exit(0);
        }
        config->data.firstGuessS.push_back(value);
      }
      i++;
    }
    fileS.close();
    if (config->data.firstGuessS.size() != config->data.firstGuessP.size()) {
      std::cout << "First guess P and S waves velocities don't have the same number of points. Terminating..." << std::endl;
      exit(0);
    }
    // ******** DISCRETE WAVELET TRANSFORM IMPLEMENTATION*********"
    dwt(config->data.firstGuessS, config->ndwts, config->wavelet, config->coeffsS,config->flagS, config->lengthS); // Performs J-Level DWT
    keepNsignificantValues(&config->coeffsS,config->npu); // Keep the N biggest absolute values in vector. Put the others to 0
    idwt(config->coeffsS,config->flagS,config->wavelet,config->data.filtFirstGuessS,config->lengthS);  // Performs IDWT with approximated coefficients
    if (config->mpiConfig.rank == 0)  // (In case of parallel implementation just one process has to create files)
      write_two_columns_file(&config->data.zp,&config->data.filtFirstGuessS, config->outputDir+"filteredFirstGuessS."+config->code+".dat");
    if (config->verbose1 && config->mpiConfig.rank == 0)
      std::cout << "  First guess files loaded (and wavelet transform performed)" << std::endl;
  }
  else
    if (config->verbose1 && config->mpiConfig.rank == 0)
      std::cout << "  First guess file loaded (and wavelet transform performed)" << std::endl;
}

void loadRealProfiles(Configuration* config)
// Load the real profiles from NAME_OF_REAL_PROFILE_FILE_P (and NAME_OF_REAL_PROFILE_FILE_S)
{
  std::string realP(config->filesDir+config->name_of_real_profile_P);
  std::ifstream fileP(realP.c_str());
  std::vector<double> zp;
  if(fileP == NULL) {
    std::cout << "IMPOSSIBLE TO OPEN "+config->filesDir+config->name_of_real_profile_P << std::endl;
    exit(0);
  }
  std::string lineP;
  while(getline(fileP, lineP)) {
    lineP = trim(lineP);
    if (!lineP.empty()) {
      std::stringstream slineP(lineP);
      double zpReal,velP;
      slineP >> zpReal >> velP ;
      config->data.realP.push_back(velP);
      zp.push_back(zpReal);
    }
  }
  fileP.close();
  if(zp.size() == config->data.zp.size()) {
    for (int i=0;i<(int)config->data.zp.size();i++) {
      if (fabs(zp[i]-config->data.zp[i]) > TINYVAL) {
        std::cout << "The depth from P wave velocity real profile are not consistent with those from P wave velocity first guess.";
        std::cout << "Terminating..." << std::endl;
        exit(0);
      }
    }
  }
  else {
    std::cout << "The depth from P wave velocity real profile are not consistent with those from P wave velocity first guess.";
    std::cout << "Terminating..." << std::endl;
    exit(0);
  }
  
  if(config->swaves) {
    std::string realS(config->filesDir+config->name_of_real_profile_S);
    std::ifstream fileS(realS.c_str());
    if(fileS == NULL) {
      std::cout << "IMPOSSIBLE TO OPEN "+config->filesDir+config->name_of_real_profile_S << std::endl;
      exit(0);
    }
    std::string lineS;
    int i=0;
    while(getline(fileS, lineS)) {
      lineS = trim(lineS);
      if (!lineS.empty()) {
        std::stringstream slineS(lineS);
        double zpReal,velS;
        slineS >> zpReal >> velS ;
        if (fabs(zpReal-config->data.zp[i]) > TINYVAL) {
          std::cout << "Depths read in "+config->filesDir+config->name_of_real_profile_S+" are not consistent with those in : ";
          std::cout << config->filesDir+config->name_of_real_profile_P+"! Terminating..." << std::endl;
          exit(0);
        }
        config->data.realS.push_back(velS);
      }
      i++;
    }
    fileS.close();
    if (config->verbose1 && config->mpiConfig.rank == 0)
      std::cout << "  Real velocity profiles loaded" << std::endl;
  }
  else
    if (config->verbose1 && config->mpiConfig.rank == 0)
      std::cout << "  Real velocity profile loaded" << std::endl;
}

void loadData(Configuration* config)
// Load the data (arrival times, first guesses, sources and receivers positions) contained on the files
{
  if (config->verbose1 && config->mpiConfig.rank == 0)
    std::cout << "Loading the data..." << std::endl;
  loadStationsPositions(config);
  loadShotsPositions(config);
  if (!config->buildPrior)
    loadPriorFeatures(config);
  loadFirstGuesses(config);
  if (config->analyticalRun) // If we perform an analytical run...
    loadRealProfiles(config); // ... load the real profiles
  else // otherwise... 
    loadArrivalTimes(config); // we load the arrival times
  copyDataFiles(config); // Copy the data files used during the simulation in OUTPUT_FILES
  if (config->verbose1 && config->mpiConfig.rank == 0)
    std::cout << "Loading done !" << std::endl << std::endl;
}

void buildPrior(Configuration* config)
// Record the most significant coefficients of the filtered first guess profiles in config->data.indexParameters
// define their maximum variation ranges (config->data.minParameters, config->data.maxParameters) during the rest of the algorithm.
// Calculate prior energy.
{
  if (config->verbose1 && config->mpiConfig.rank == 0)
    std::cout << "Building the a priori space..." << std::endl;
  for (unsigned int i = 0; i < config->coeffsP.size(); i++) { // Loop on the P coeffs
    if (fabs(config->coeffsP[i]) > 0) {
      config->data.maxParameters.push_back((1.0+sign(config->coeffsP[i])*config->A)*config->coeffsP[i]);
      config->data.minParameters.push_back((1.0-sign(config->coeffsP[i])*config->A)*config->coeffsP[i]);
      config->data.indexParameters.push_back((int)i);
    }
  }
  if (config->swaves) {
    for (unsigned int i = 0; i < config->coeffsS.size(); i++) { // Loop on the S coeffs
      if (fabs(config->coeffsS[i]) > 0) {
        config->data.maxParameters.push_back((1.0+sign(config->coeffsS[i])*config->A)*config->coeffsS[i]);
        config->data.minParameters.push_back((1.0-sign(config->coeffsS[i])*config->A)*config->coeffsS[i]);
        config->data.indexParameters.push_back((int)i);
      }
    }
  }
  if (config->mpiConfig.rank == 0)
    write_three_columns_file(&config->data.indexParameters,&config->data.minParameters, &config->data.maxParameters,config->outputDir+"priorFeatures."+config->code+".dat");
  
  double Ep=1.0;
  for(int i=0;i<(int)config->data.minParameters.size();i++) // Loop on the parameters to calculate the prior probability
    Ep*=fabs(config->data.maxParameters[i]-config->data.minParameters[i]);
  config->data.Ep=log(Ep);
  // Important! We use these vectors in InverseWaveletTransform to store the coefficient (that is stupid by the way...) TODO :
  for(int i=0;i<(int)config->coeffsP.size();i++) 
    config->coeffsP[i]=0.0; 
  for(int i=0;i<(int)config->coeffsS.size();i++) 
    config->coeffsS[i]=0.0;

  if (config->verbose1 && config->mpiConfig.rank == 0)
    std::cout << "Done ! (energy of the prior : " << config->data.Ep << ")" << std::endl << std::endl;
}

void generate_profiles_from_prior(Configuration* config)  
// Generate config.nPriorProfiles profiles from a priori space in config.outputDir/priorProfilesXXX/
{
  if (config->verbose1 && config->mpiConfig.rank == 0 && config->nPriorProfiles > 0) {
    std::cout << "Storing " << config->nPriorProfiles << " profiles from the prior space in ";
    std::cout <<  config->outputDir+"priorProfiles"+config->code+"/" << std::endl;
  }
  for(int i=0;i<config->nPriorProfiles;i++) {
    std::vector<double> params;
    std::vector<double> priorProfileP, priorProfileS;
    if (config->verbose2 && config->mpiConfig.rank == 0)
      std::cout << "  Profile " << i+1 << " :" << std::endl;
    for(int j=0;j<(int)config->data.indexParameters.size();j++) {  // Loop on the number of parameters that we will modify
      params.push_back(Uniform(config->data.minParameters[j],config->data.maxParameters[j]));
      if (config->verbose2 && config->mpiConfig.rank == 0) {
        std::cout<< "  params["<< j <<"] : " << params[j] << "   ";
        std::cout<< "(Uniform between : "<< config->data.minParameters[j] <<" and " << config->data.maxParameters[j] << ") " << std::endl;
      }
    }
    if (config->verbose2 && config->mpiConfig.rank == 0)
      std::cout << std::endl;
    InverseWaveletTransform(&priorProfileP,&params, config, false); // Calculate the P waves velocity profile corresponding to the wavelet coefficients
    if(config->swaves)
      InverseWaveletTransform(&priorProfileS,&params, config, true); // Calculate the S waves velocity profile corresponding to the wavelet coefficients
    std::string mkdir_command="mkdir -p "+config->outputDir+"priorProfiles"+config->code+"/";
    int status;
    if (config->mpiConfig.rank == 0) {
      status = system(mkdir_command.c_str()); // TODO : This is a little bit dirty but there is no simple equivalent to "mkdir -p"
      if(status != 0) {
        std::cout << "Error while creating "+config->outputDir+"priorProfiles"+config->code+"/"+" (status = "<< status << ")" << std::endl;
        exit(1);
      }
    }
    std::ostringstream ii;   // Store i as a string
    ii << i;
    if (config->mpiConfig.rank == 0) {
      write_two_columns_file(&config->data.zp, &priorProfileP, config->outputDir+"priorProfiles"+config->code+"/"+"priorProfileP."+config->code+"."+ii.str()+".dat");
      if(config->swaves)
        write_two_columns_file(&config->data.zp, &priorProfileS, config->outputDir+"priorProfiles"+config->code+"/"+"priorProfileS."+config->code+"."+ii.str()+".dat");
    }
  }
  if (config->verbose1 && config->mpiConfig.rank == 0 && config->nPriorProfiles > 0)
    std::cout << "Done ! " << std::endl << std::endl;
}

void designSmallGrid(Configuration* config)
// Calculate optimum nx,ny,nzFilt,dx,dy,dzFilt to reduce computation time.
{

  if(config->verbose2 && config->mpiConfig.rank == 0) 
    std::cout << "Designing small grid..." << std::endl;
  // Calculate min and max values of coordinates given in coordStats and coordShots files
  double xmin,ymin,zmin,xmax,ymax,zmax; // Will contain min and max values of coordinates given in coordStats and coordShots files
  std::vector<double> xs,ys,zs; // Will contain all x and y coordinates
  for (int istat=0;istat<(int)config->data.coordStations.size();istat++) {
    xs.push_back(config->data.coordStations[istat].x);
    ys.push_back(config->data.coordStations[istat].y);
    zs.push_back(config->data.coordStations[istat].z);
  }
  for (int ishot=0;ishot<(int)config->data.coordShots.size();ishot++) {
    xs.push_back(config->data.coordShots[ishot].x);
    ys.push_back(config->data.coordShots[ishot].y);
    zs.push_back(config->data.coordShots[ishot].z);
  }
  xmax = *std::max_element(xs.begin(), xs.end());
  ymax = *std::max_element(ys.begin(), ys.end());
  zmax = *std::max_element(zs.begin(), zs.end());
  xmin = *std::min_element(xs.begin(), xs.end());
  ymin = *std::min_element(ys.begin(), ys.end());
  zmin = *std::min_element(zs.begin(), zs.end());

  double zminFirstGuess = *std::min_element(config->data.z.begin(), config->data.z.end()); // min values of z coordinates given in first guess file
  double zmaxFirstGuess = *std::max_element(config->data.z.begin(), config->data.z.end()); // max values of z coordinates given in first guess file

  if ((xmin == xmax) || (ymin == ymax) || (zmin == zmax)) { // (**)
    std::cout << "This is a 2D (or 1D) problem... Using FTeik 3D would be stupid ! We should use FTeik2D (or a simple t=d/v ";
    std::cout << "if the problem is 1D) but it is not implemented for now. Terminating..." << std::endl;
    exit(0);
  }  
  if ((zmin < zminFirstGuess) || (zmax > zmaxFirstGuess)) {
    std::cout << "z minimum or maximum values given in "+config->filesDir+config->name_of_stations_file+" and ";
    std::cout << config->filesDir+config->name_of_shots_file+" are inconsistent with those described by ";
    std::cout << config->filesDir+config->name_of_first_guess_P_file+". Terminating..." << std::endl;
    std::cout << "zmin : " << zmin << " zminFirstGuess : "<< zminFirstGuess << " zmax : " << zmax << " zmaxFirstGuess : "<< zmaxFirstGuess << std::endl;
    exit(0);
  }

  double xminGrid = xmin - (xmax-xmin)*config->coordTol;
  double xmaxGrid = xmax + (xmax-xmin)*config->coordTol;
  double yminGrid = ymin - (ymax-ymin)*config->coordTol;
  double ymaxGrid = ymax + (ymax-ymin)*config->coordTol;
  config->data.xminGrid = xminGrid;
  config->data.yminGrid = yminGrid;
  config->data.zminGrid = config->data.z[0];
  if(config->verbose2 && config->mpiConfig.rank == 0) {
    std::cout << "  Min x coordinate : " << xmin << " Max x coordinate "<< xmax << " Min y coordinate : " << ymin << " Max y coordinate : "<< ymax << std::endl;
    std::cout << "  Min x of grid : " << xminGrid << " Max x of grid "<< xmaxGrid << " Min y of grid : " << yminGrid << " Max y of grid : "<< ymaxGrid << std::endl;
    std::cout << "  Min z : " << zmin << " Min z of grid : " << config->data.zminGrid << std::endl;
    std::cout << "  Max z : " << config->data.z.back() << " Min z of grid : " << config->data.zminGrid << std::endl << std::endl;
  }
  // Find min relatives intervals between two stations and/or receivers (expressed as a fraction of the maximum distances xmax and ymax)
  // Concatenate xstat with xshots -> xcoords, and ystat with yshots -> ycoords
  std::vector<double> dx,dy; // Will contain the relatives intervals
  for (int ix1=0;ix1<(int)xs.size()-1;ix1++) {
    for (int ix2=ix1+1;ix2<(int)xs.size();ix2++) {
      //std::cout << "(ix1,ix2) : (" << ix1 << "," << ix2 << ")" << std::endl;
      if (fabs(xs[ix1]-xs[ix2]) > TINYVAL) // We just look at distinct coordinates
        dx.push_back(fabs(xs[ix1]-xs[ix2]));
      if (fabs(ys[ix1]-ys[ix2]) > TINYVAL) // We just look at distinct coordinates
        dy.push_back(fabs(ys[ix1]-ys[ix2]));
    }
  }
  double dxmin = -1.0,dymin=-1.0;
  if (dx.size()>0) // If there is distinct coordinates. Otherwise the problem is 2D or 1D! This must not happen! See (**) above
    dxmin = *std::min_element(dx.begin(), dx.end()); // Minimum x distances between two stations and/or receivers
  if (dy.size()>0) // If there is distinct coordinates. Otherwise the problem is 2D or 1D! This must not happen! See (**) above
    dymin = *std::min_element(dy.begin(), dy.end()); // Minimum y distances between two stations and/or receivers
  
  findOptimumGrid(xmaxGrid,ymaxGrid,dxmin,dymin,config); // if config->findOptimumGrid = 1 :
  // Filter a first guess curve iteratively and run the eikonal each time to determine nx,ny and nzFilt to use 
  // if config->findOptimumGrid = 0 use : 
  // noXptdtsd = config->numberOfXpointsToDescribeTheSmallestDistanceDefault
  // noYptdtsd = config->numberOfYpointsToDescribeTheSmallestDistanceDefault
  // nzFilt = config->nzfiltDefault
  //
  // --> Then calculate dx,dy and dzFilt
}

void findOptimumGrid(double xmaxGrid, double ymaxGrid, double dxmin, double dymin, Configuration* config)
// Filter a first guess curve iteratively and run the eikonal each time to determine nx,ny and nzFilt to use. 
// Then calculate dx,dy and dzFilt
// We just do that for P waves for now TODO : same for S waves as well
{

  int noXptdtsd = 0,noYptdtsd = 0; // Number Of Points To Describe The Smallest Distances
  int nx = 0, ny = 0, nzFilt = 0;
  float dx = 0.0, dy = 0.0, dzFilt = 0.0;
  float dxRef = 0.0, dyRef = 0.0, dzRef = 0.0;
  int shotNumber=config->shotNumberRef;
  std::vector<double> zFilt, zFiltp;
  std::vector<double> nxs,nys,nzs,dxs,dys,dzs,noXptdtsdRecord,noYptdtsdRecord,averageDiffs;
   
  if(config->findOptimumGrid) {
    if(config->verbose1 && config->mpiConfig.rank == 0) {
      std::cout << "  -> Looking for optimum grid " << std::endl;
      std::cout << "    Some runs will be performed to find an optimum grid." << std::endl;
      std::cout << "    First, we construct references times with a precise grid. We extend the first guess P wave velocity profile on that grid." << std::endl;
      std::cout << "    (~"+formatBytes(sizeof(double)*config->nxref*config->nyref*config->data.nz+sizeof(double)*(config->nxref-1)*(config->nyref-1)*(config->data.nz-1))+" will be used... are you sure to have enough memory?)" << std::endl;
    }
    tab3d<double> refTt3dP(config->data.nz,config->nxref,config->nyref,-1.0); // Will contain the P waves arrival times. Initialize every cell at -1.0 
    VelocityModel refVelModel;    // Will contain the reference velocity model
    refVelModel.nx = config->nxref; refVelModel.ny = config->nyref; refVelModel.nz = config->data.nz;
    dxRef = (xmaxGrid-config->data.xminGrid)/(config->nxref-1);
    dyRef = (ymaxGrid-config->data.yminGrid)/(config->nyref-1);
    dzRef = config->data.dz;
    refVelModel.dx = dxRef; refVelModel.dy = dyRef; refVelModel.dz = dzRef;
    refVelModel.xmin = config->data.xminGrid; refVelModel.ymin = config->data.yminGrid; refVelModel.zmin = config->data.zminGrid;
    refVelModel.velP= new tab3d<double>(refVelModel.nz-1,refVelModel.nx-1,refVelModel.ny-1,-1.0); // Create a (nz-1,nx-1,ny-1) mesh and initialize every cell at -1 
    meshing(&config->data.filtFirstGuessP,&refVelModel,false); // Extend this profile on the whole mesh (it is the filteredFirstGuessP.XXX.dat)
    if(config->verbose1 && config->mpiConfig.rank == 0) {
      std::cout << "      Reference Eikonal computing for nx = config->nxref = " << config->nxref << " ny = config->nyref = " << config->nyref << " nz = ";
      std::cout << config->data.nz << "     (shot number " << shotNumber << ")... " << std::endl;
      std::cout << "      This could take some time..." << std::endl;
    }
    // Calculate the P waves travel times everywhere on the mesh (put them on tt3dP) for the shot number shotNumber :
    eikonal3d(&refTt3dP,&refVelModel,config,shotNumber,false); 
    delete refVelModel.velP;
    if(config->verbose1 && config->mpiConfig.rank == 0) {
      std::cout << "      Done !" << std::endl << std::endl;
      std::cout << "    Now we will compare the times from this large run to iteratively decimated version of P wave velocity profile... " << std::endl << std::endl;
    }    
    std::vector<int> noXptdtsdVec,noYptdtsdVec; // Number Of Points To Describe The Smallest Distances, vectors to store the values to test.
    std::vector<int> nzFiltVec; // nz for filtered models

    noXptdtsdVec=config->noXptdtsdVec; //.push_back(4);noXptdtsdVec.push_back(5);//noXptdtsdVec.push_back(4);noXptdtsdVec.push_back(5);
    noYptdtsdVec=config->noYptdtsdVec; //.push_back(4);noYptdtsdVec.push_back(5);//noYptdtsdVec.push_back(4);noYptdtsdVec.push_back(5);
    nzFiltVec=config->nzFiltVec; //.push_back(100);nzFiltVec.push_back(150);//nzFiltVec.push_back(200);nzFiltVec.push_back(300);
    VelocityModel velModel;    // Will contain the velocity model
  
    for (int inzFilt=0;inzFilt<(int)nzFiltVec.size();inzFilt++) { 
      for (int inoXptdtsd=0;inoXptdtsd<(int)noXptdtsdVec.size();inoXptdtsd++) {
        for (int inoYptdtsd=0;inoYptdtsd<(int)noYptdtsdVec.size();inoYptdtsd++) {
          nzFilt = nzFiltVec[inzFilt];
        //  noXptdtsd = noXptdtsdVec[inoXptdtsd];
        //  noYptdtsd = noYptdtsdVec[inoYptdtsd];
        //  dx=dxmin/((float)noXptdtsd);
        //  dy=dymin/((float)noYptdtsd);
        //  nx=ceil((xmaxGrid-config->data.xminGrid)/dx);
        //  ny=ceil((ymaxGrid-config->data.yminGrid)/dy);
          nx = noXptdtsdVec[inoXptdtsd]; // TODO rename noXptdtsdVec to nxVec ...
          ny = noYptdtsdVec[inoXptdtsd]; // TODO rename noYptdtsdVec to nyVec ...
          dx = (xmaxGrid-config->data.xminGrid)/((float)(nx-1));
          dy = (ymaxGrid-config->data.yminGrid)/((float)(ny-1));
          dzFilt=config->data.dz*((double)config->data.nz-1)/((double)nzFilt-1.0);
          /*
          z[1]   -        zFilt[1]     -            ^
                dz                  dzFilt          |
          z[2]   -                                  |
                          zFilt[2]     -            |
          z[3]   -   --->                           |
                                                    |
          z[4]   -        zFilt[3]     -            | zmax-zmin = (nz-1)*dz = (nzFilt-1)*dzFilt   =>   dzFilt = dz*(nz-1)/(nzFilt-1)
                                                    |
          z[5]   -                                  |
                                                    |
           ...                ...                   |
                                                    |
          z[nz] -        zFilt[nzFilt] -            v
         
         !! Warning indices start at 0 (to nz-1) !! 
          */
          
          nxs.push_back(nx);
          nys.push_back(ny);
          nzs.push_back(nzFilt);
          dxs.push_back(dx);
          dys.push_back(dy);
          dzs.push_back(dzFilt);
          noXptdtsdRecord.push_back(noXptdtsd);
          noYptdtsdRecord.push_back(noYptdtsd);
          if(config->verbose2 && config->mpiConfig.rank == 0) {
            std::cout << "    Number Of x Points To Describe The Smallest Distances : " << noXptdtsd << std::endl; 
            std::cout << "    Number Of y Points To Describe The Smallest Distances : " << noYptdtsd << std::endl;
            std::cout << "    nx = " << nx << " ny = " << ny << " nzFilt = " << nzFilt;
            std::cout << "    dx = " << dx << " dy = " << dy << " dzFilt = " << dzFilt << std::endl;
            std::cout << "    (~"+formatBytes(sizeof(double)*nx*ny*nzFilt+sizeof(double)*(nx-1)*(ny-1)*(nzFilt-1))+" will be used... are you sure to have enough memory?)" << std::endl;
          }
          zFilt=linspace(config->data.z[0],config->data.z.back(),nzFilt);
          // Build zFiltp : it will contains the real positions of the velocities vpFilt calculated after that
          zFiltp.clear(); // Clear the vector leaving its size to 0
          for(int i=1;i<(int)zFilt.size();i++) // zFiltp give z in the middle of intervals
            zFiltp.push_back(zFilt[i-1]+(zFilt[i]-zFilt[i-1])/2.0);
          velModel.nx = nx; velModel.ny = ny; velModel.nz = nzFilt;
          velModel.dx = dx; velModel.dy = dy; velModel.dz = dzFilt;
          velModel.xmin = config->data.xminGrid; velModel.ymin = config->data.yminGrid; velModel.zmin = config->data.zminGrid;
          std::vector<double> velP1D = downSampleProfile(config->data.filtFirstGuessP,config->data.zp,zFiltp);
          // velP1D Contains the down sampled velocity model !! warning velP will not give the velocity at zFilt but between the points. It will have a size nzFilt-1
          velModel.velP= new tab3d<double>(velModel.nz-1,velModel.nx-1,velModel.ny-1,-1.0); // Create a (nz-1,nx-1,ny-1) mesh and initialize every cell at -1 
            // Copy the file content into the velocity model (extend the 1D profile to obtain a 3D profile).
          meshing(&velP1D,&velModel,false); // Extend this profile on the whole mesh
          tab3d<double> tt3dP(nzFilt,nx,ny,-1.0); // Will contain the P waves arrival times. Initialize every cell at -1.0 
          if(config->verbose2 && config->mpiConfig.rank == 0)
            std::cout << "      Eikonal computing for this model... " << std::endl;
          // Calculate the P waves travel times everywhere on the mesh (put them on tt3dP) for the shot number shotNumber :
          eikonal3d(&tt3dP,&velModel,config,shotNumber,false); 
          // Here compare tt with a big run
          if(config->verbose2 && config->mpiConfig.rank == 0)
            std::cout << "      Done !" << std::endl;
          delete velModel.velP;
          double averageDiff = 0;
          for (int k=0; k<(int)config->data.coordStations.size();k++) {
           // std::cout << "  tt3dP[" << k << "] : "  << getTime(&tt3dP,config->data.coordStations[k],&velModel);
           // std::cout << "  refTt3dP[ " << k << "] : " << getTime(&refTt3dP,config->data.coordStations[k],&refVelModel);
           // std::cout << "  diff : " << fabs(getTime(&tt3dP,config->data.coordStations[k],&velModel) -getTime(&refTt3dP,config->data.coordStations[k],&refVelModel)) << std::endl;
            averageDiff += fabs(getTime(&tt3dP,config->data.coordStations[k],&velModel) -getTime(&refTt3dP,config->data.coordStations[k],&refVelModel));
          }
          averageDiff /= (double)config->data.coordStations.size();
          averageDiffs.push_back(averageDiff);
          if(config->verbose2 && config->mpiConfig.rank == 0)
            std::cout << "    Average differences in seconds at the receivers between big run and that run for shot number "<< shotNumber << " : "  << averageDiff << std::endl << std::endl;
        }
      }
    }
    int min_index = std::min_element(averageDiffs.begin(), averageDiffs.end()) - averageDiffs.begin(); // This gives the index of the minimum average value
    if(config->verbose1 && config->mpiConfig.rank == 0) {
      std::cout << "    The best grid configuration among those tested ("<< (int)averageDiffs.size() << " tested) seems to be :" << std::endl;
      std::cout << "      Number Of x Points To Describe The Smallest Distances : " << noXptdtsdRecord[min_index] << std::endl; 
      std::cout << "      Number Of y Points To Describe The Smallest Distances : " << noYptdtsdRecord[min_index] << std::endl;
      std::cout << "      nx = " << nxs[min_index] << " ny = " << nys[min_index] << " nzFilt = " << nzs[min_index] << std::endl;
      std::cout << "      dx = " << dxs[min_index] << " dy = " << dys[min_index] << " dzFilt = " << dzs[min_index] << std::endl;
      std::cout << "      -> Average differences at the receivers between big run and that run for shot number "<< shotNumber << " : "  << averageDiffs[min_index] << std::endl << std::endl;
      std::cout << "    We keep this grid configuration !" << std::endl;
      std::cout << "Done!" << std::endl << std::endl;
    }
    config->data.nzFilt=nzs[min_index];
    config->data.nx=nxs[min_index];
    config->data.ny=nys[min_index];
    config->data.dzFilt=dzs[min_index];
    config->data.dx=dxs[min_index];
    config->data.dy=dys[min_index];
  } // If config->findOptimumGrid == 0, we use values given in the configuration file :
  else {
    config->data.nzFilt=config->nzfiltDefault;
    config->data.dx=dxmin/((float)config->numberOfXpointsToDescribeTheSmallestDistanceDefault);
    config->data.dy=dymin/((float)config->numberOfYpointsToDescribeTheSmallestDistanceDefault);
    config->data.nx=ceil((xmaxGrid-config->data.xminGrid)/config->data.dx);
    config->data.ny=ceil((ymaxGrid-config->data.yminGrid)/config->data.dy);
    config->data.dzFilt=config->data.dz*((double)config->data.nz-1)/((double)config->data.nzFilt-1.0);
    if(config->verbose1 && config->mpiConfig.rank == 0) {
      std::cout << "  -> We use the grid configuration given :" << std::endl;
      std::cout << "    Number Of x Points To Describe The Smallest Distances : " << config->numberOfXpointsToDescribeTheSmallestDistanceDefault << std::endl; 
      std::cout << "    Number Of y Points To Describe The Smallest Distances : " << config->numberOfYpointsToDescribeTheSmallestDistanceDefault << std::endl;
      std::cout << "    nx = " << config->data.nx << " ny = " << config->data.ny << " nzFilt = " << config->data.nzFilt << std::endl;
      std::cout << "    dx = " << config->data.dx<< " dy = " << config->data.dy << " dzFilt = " << config->data.dzFilt << std::endl;
    }
  }
  config->data.zFilt=linspace(config->data.z[0],config->data.z.back(),config->data.nzFilt);
  // Build zFiltp : it will contains the real positions of the velocities vpFilt calculated after that
  for(int i=1;i<(int)config->data.zFilt.size();i++) // zFiltp give z in the middle of intervals
    config->data.zFiltp.push_back(config->data.zFilt[i-1]+(config->data.zFilt[i]-config->data.zFilt[i-1])/2.0);
  std::vector<double> velP1D = downSampleProfile(config->data.filtFirstGuessP,config->data.zp,config->data.zFiltp);
  if(config->mpiConfig.rank == 0)
    write_two_columns_file(&config->data.zFiltp,&velP1D, config->outputDir+"downSampledFiltredFirstGuessP."+config->code+".dat");
} 

void createDataset(Configuration* config)
// Create the arrival times from the real profile. TODO : parallelize this function
{
  if(config->swaves) { 
    if(config->verbose1 && config->mpiConfig.rank == 0) {
      std::cout << "This is an analytical run : we create the dataset from the real velocity profiles given... ";
      std::cout << "(files : " << config->filesDir+config->name_of_real_profile_P << " and " ;
      std::cout << config->filesDir+config->name_of_real_profile_S << ")"<< std::endl;
    }
  }
  else {
    if(config->verbose1 && config->mpiConfig.rank == 0) {
      std::cout << "This is an analytical run, we first begin by creating the dataset from the real velocity profile...";
      std::cout << "(file : " << config->filesDir+config->name_of_real_profile_P << ")" << std::endl << std::endl;
    }
  }
  VelocityModel velModel;    // Will contain the velocity model
  ArrivalTimes arrivalTimes; // Will contain the P and S waves arrival times at receivers positions
  if (config->data.z[1] > config->data.z[0])
    config->data.dz = config->data.z[1]-config->data.z[0]; // In this case (analytical) we don't use the default dz but we calculate it
  else {
    std::cout << "By now z must point downwards. Terminating..." << std::endl;
    exit(0);
  }
  config->data.nz = config->data.realP.size()+1; // We add one because there is on border point more than intervals (see README)
  velModel.nx = config->data.nx; velModel.ny = config->data.ny; velModel.nz = config->data.nz;  
  velModel.dx = config->data.dx; velModel.dy = config->data.dy; velModel.dz = config->data.dz;//fabs(config->data.z[1]-config->data.z[0]);
  velModel.xmin = config->data.xminGrid; velModel.ymin = config->data.yminGrid; velModel.zmin = config->data.zminGrid;
  velModel.velP= new tab3d<double>(velModel.nz-1,velModel.nx-1,velModel.ny-1,-1.0); // Create a (nz-1,nx-1,ny-1) mesh and initialize every cell at -1 
  // Copy the file content into the velocity model (extend the 1D profile to obtain a 3D profile).
  meshing(&config->data.realP,&velModel,false); // Extend this profile on the whole mesh
  tab3d<double> tt3dP(velModel.nz,velModel.nx,velModel.ny,1.0); // Will contain the P waves arrival times. Initialize every cell at 1.0 
  if(config->verbose1 && config->mpiConfig.rank == 0)
    std::cout << "Eikonal calculations for P waves... " << std::endl;
  for(int shotNumber=0;shotNumber<(int)config->data.coordShots.size();shotNumber++) 
  {
    if(config->verbose1 && config->mpiConfig.rank == 0)
      std::cout << "  Shot Number " << shotNumber+1 << " on " << config->data.coordShots.size() << std::endl;  
    // Calculate the P waves travel times everywhere on the mesh (put them on tt3dP) for the shot number shotNumber :
    eikonal3d(&tt3dP,&velModel,config,shotNumber,false); 
    for(int j=0;j<(int)config->data.coordStations.size();j++)
      arrivalTimes.timesP.push_back(getTime(&tt3dP,config->data.coordStations[j],&velModel));
  }
  delete velModel.velP;
  config->data.times.timesP = arrivalTimes.timesP;

  if(config->swaves) {
    velModel.velS= new tab3d<double>(velModel.nz-1,velModel.nx-1,velModel.ny-1,-1); // Create a (nz-1,nx-1,ny-1) mesh and initialize every cell at -1
    meshing(&config->data.realS,&velModel,true); // Extend this profile on the whole mesh
    tab3d<double> tt3dS(velModel.nz,velModel.nx,velModel.ny,1.0); // Will contain the S waves arrival times. Initialize every cell at 1.0 
    if(config->verbose1 && config->mpiConfig.rank == 0)
      std::cout << std::endl << "Eikonal calculations for S waves... " << std::endl;
    for(int shotNumber=0;shotNumber<(int)config->data.coordShots.size();shotNumber++) 
    {
      if(config->verbose1 && config->mpiConfig.rank == 0)
        std::cout << "  Shot Number " << shotNumber+1 << " on " << config->data.coordShots.size() << std::endl;  
       // Calculate the S waves travel times everywhere on the mesh (put them on tt3dS) for the shot number shotNumber :
      eikonal3d(&tt3dS,&velModel,config,shotNumber,true);
      for(int j=0;j<(int)config->data.coordStations.size();j++)
        arrivalTimes.timesS.push_back(getTime(&tt3dS,config->data.coordStations[j],&velModel));
    }
    delete velModel.velS;
    config->data.times.timesS = arrivalTimes.timesS;
    if(config->mpiConfig.rank == 0)
      write_two_columns_file(&config->data.times.timesP,&config->data.times.timesS, config->outputDir+"calculatedTimes."+config->code+".dat");
  }
  else
    if(config->mpiConfig.rank == 0)
      write_one_column_file(&config->data.times.timesP, config->outputDir+"calculatedTimes."+config->code+".dat");
    if(config->verbose1 && config->mpiConfig.rank == 0) {
      std::cout << "Done !" << std::endl;
      std::cout << "The calculated arrival times has been written in : "+config->outputDir+"calculatedTimes."+config->code+".dat";
      std::cout << std::endl << std::endl;
    }
}

State init_state(Configuration* config)
// Initialize a markov chain state
{
  State state;
  if (config->swaves)
    state.params=std::vector<double>(config->npu*2); // Create a vector that will contain the parameters describing the state
  else
    state.params=std::vector<double>(config->npu); // Create a vector that will contain the parameters describing the state
  for(int i=0;i<(int)state.params.size();i++) { // Loop on the parameters
    // Draw the parameter number i of the state in an uniform prior 
    state.params[i]=Uniform(config->data.minParameters[i],config->data.maxParameters[i]);
    state.E=0;  // We need the value of the temperature to calculate the energy and so the chain in which it is,
    //for the moment we are just considering a single state out of a chain
    if (config->verbose2 && config->mpiConfig.rank == 0) {
      std::cout<< "Init state param["<< i <<"] : " << state.params[i] << "   ";
      std::cout<< "(Uniform between : "<< config->data.minParameters[i] << " and " << config->data.maxParameters[i] << ") " << std::endl;
    }
  }
  return state;
}

Run init_run(Configuration* config)
// Initialize the run, create each chain at each temperature and the first state of each one.
{
  Run run;
  std::vector<long double> line(config->nbt-1); // This will contain the first line of the matrix of importance weights
  for(int i=0;i<config->nbt;i++) {              // Loop on the temperatures : i.e the number of chains
    if(config->verbose1 && config->mpiConfig.rank == 0)
      std::cout << "Initialization of chain " << i+1 << " on " << config->nbt << " (Temperature : "<< config->T[i] << ") " << std::endl;
    State initialState;                     // Create a new state
    initialState=init_state(config);        // And initialize it
    Chain* chain = new Chain();            // Create a pointer on a Chain
    chain->states.push_back(initialState); // Put the initialState on it
    chain->nc=config->nc[i];               // Copy from the config the nb of component to modify in a MH iteration for this chain
    chain->T=config->T[i];                 // Copy from the config the temperature of this chain
    chain->i=i;                            // Store the index of the chain
    // Initialize the statistics of the chain :
    chain->at=0;
    chain->rt=0;
    chain->od=0;
    chain->ps=0;
    chain->as=0;
    chain->rs=0;
    std::vector<double> L;
    for(int j=0;j<(int)chain->states[0].params.size();j++) {
      L.push_back(config->data.maxParameters[j]-config->data.minParameters[j]);
      // At T=Tmax deltaState[i]=L[i]/di
      // At T=1    deltaState[i]=L[i]/df
      double a = 0.;
      double b = 0.;
      if (fabs(config->tmax-1) > TINYVAL) { // false if prior iteration for example
        a = L[j]*(1./config->df-1./config->di)/(1.-config->tmax);
        b = L[j]*(1./config->di-config->tmax/config->df)/(1.-config->tmax);
      } 
      chain->deltaParameters.push_back(a*chain->T+b); // The variation range of the parameters varies with the temperature
    }
    run.chains.push_back(chain); // Add this chain to the run
    // We will keep all the profiles generated by the chains to calculate the quantiles :
    for(int iz=0;iz<config->data.nzFilt-1;iz++) {
      std::vector<double> values;
      run.chains[i]->profilesP.push_back(values); // The profiles will be filled by "energy" function
      if (config->swaves)
        run.chains[i]->profilesS.push_back(values);
    }
    
    run.chains[i]->states[0].E=energy(&initialState,chain,config); // Compute the energy of the first state of the chain
    
    for(int iz=0;iz<config->data.nzFilt-1;iz++) {
      run.chains[i]->minP.push_back(run.chains[i]->profilesP[iz].back()); // When there is just one profile it is the minimum and the maximum :
      run.chains[i]->maxP.push_back(run.chains[i]->profilesP[iz].back());
      if (config->swaves) {
        run.chains[i]->minS.push_back(run.chains[i]->profilesS[iz].back());
        run.chains[i]->maxS.push_back(run.chains[i]->profilesS[iz].back());
      }
      run.chains[i]->averageP.push_back(run.chains[i]->profilesP[iz].back()); //At the beginning the average profile is the first one
      run.chains[i]->varP.push_back(0.);
      run.chains[i]->qInfP.push_back(0.);
      run.chains[i]->qSupP.push_back(0.);
      if (config->swaves) {
        run.chains[i]->averageS.push_back(run.chains[i]->profilesS[iz].back());
        run.chains[i]->varS.push_back(0.);
        run.chains[i]->qInfS.push_back(0.);
        run.chains[i]->qSupS.push_back(0.);
      }
    }

    if (i>0) {
      // Initialization of importance weights matrix and normalization coefficients.
      line[i-1]=(long double)expl(run.chains[i]->states[0].E *(1.-run.chains[i]->T/run.chains[i-1]->T));
    }
    //if (i>0)
    //  line[i-1]=exp(-(run.chains[i-1]->states[0].E-run.chains[i]->states[0].E));
    //  line[i-1]=exp(run.chains[i-1]->states[0].E *(run.chains[i-1]->T/run.chains[i]->T -1));
    // Initialization of importance weights matrix and normalization coefficients.
    // SCI[0][l]=exp(Ep*T[i]*(1/T[i+1]-1/T[i]));
    // delete(chain);
  }

  run.minP=run.chains[0]->minP; // First we store the profile for chain 0 (we will compare it with the others)
  run.maxP=run.chains[0]->maxP;
  if (config->swaves) {
    run.minS=run.chains[0]->minS; // First we store the profile for chain 0 (we will compare it with the others)
    run.maxS=run.chains[0]->maxS;
  }
  for(int i=1;i<config->nbt;i++) { // Loop on the other chains
    for(int iz=0;iz<config->data.nzFilt-1;iz++) { // Loop on the depths
      if (run.minP[iz] > run.chains[i]->minP[iz])
        run.minP[iz] = run.chains[i]->minP[iz];
      if (run.maxP[iz] < run.chains[i]->maxP[iz])
        run.maxP[iz] = run.chains[i]->maxP[iz];
      if (config->swaves) {
        if (run.minS[iz] > run.chains[i]->minS[iz])
          run.minS[iz] = run.chains[i]->minS[iz];
        if (run.maxS[iz] < run.chains[i]->maxS[iz])
          run.maxS[iz] = run.chains[i]->maxS[iz];
      }
    }
  }
  run.bestE.push_back(run.chains[0]->states[0].E); // We do that just to put something in that vector (we will fill it with writeBestProfiles)
  run.idxE.push_back(0);
  run.chainBestE.push_back(0);

  run.SCI.push_back(line);
  return run;
}
