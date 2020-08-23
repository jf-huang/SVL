#include "DynamicAnalysis.hpp"
#include "Definitions.hpp"
#include "Profiler.hpp"

//Default constructor:
DynamicAnalysis::DynamicAnalysis(std::shared_ptr<Mesh> &mesh, std::shared_ptr<Algorithm> &algorithm, std::shared_ptr<Integrator> &integrator, std::shared_ptr<LoadCombo> &loadcombo, unsigned int nSteps) : 
Analysis(loadcombo), theMesh(mesh), NumberOfTimeSteps(nSteps){
    //Moves the linear system algorithm to this analysis.
    theAlgorithm = std::move(algorithm); 

    //Moves the dynamic integrator to this analysis.
    theIntegrator = std::move(integrator); 

    //Sets the load combination to be used.
    theIntegrator->SetLoadCombination(loadcombo);
}

//Virtual destructor:
DynamicAnalysis::~DynamicAnalysis(){
    //Does nothing.
}

//Perform the analysis in incremental steps.
bool 
DynamicAnalysis::Analyze(){
    //Starts profiling this funtion.
    PROFILE_FUNCTION();

    //Initialize all recorders of this analysis.
    StartRecorders(theMesh, NumberOfTimeSteps);

    //Perform the incremental anlaysis in nStep increments.
    for(unsigned int k = 1; k < NumberOfTimeSteps; k++){
        //Solves until converge at i-th steps.
        bool stop = theIntegrator->ComputeNewStep(theMesh, k); 

        //Checks the solution has issues.
        if(stop){
            EndRecorders();
            return stop;
        }

        //Updates the domain state.
        UpdateDomain(k);

        //Store information in recorders.
        WriteRecorders(theMesh, k);

        //Prints the analysis progress.
        PrintProgressBar((k+1)*100/NumberOfTimeSteps);
    }

    //Finilize all recorders of this analysis.
    EndRecorders();

    //Return the analysis status.
    return false;
}

//Performs changes in mesh.
void 
DynamicAnalysis::UpdateDomain(unsigned int k){
    //Starts profiling this function.
    PROFILE_FUNCTION();

    //Obtains the total current response states.  
    Eigen::VectorXd U = theIntegrator->GetDisplacements();
    Eigen::VectorXd V = theIntegrator->GetVelocities();
    Eigen::VectorXd A = theIntegrator->GetAccelerations();

    //Gets all nodes from the mesh.
    std::map<unsigned int, std::shared_ptr<Node> >  Nodes = theMesh->GetNodes();

    for(std::map<unsigned int, std::shared_ptr<Node> >::iterator it = Nodes.begin(); it != Nodes.end(); ++it){
        unsigned int Tag = it->first;

        //Gets the associated nodal degree-of-freedom.
        std::vector<int> TotalDofs = Nodes[Tag]->GetTotalDegreeOfFreedom();
        unsigned int nTotalDofs = TotalDofs.size();

        //Creates the nodal/incremental vector state.
        Eigen::VectorXd Uij(nTotalDofs); Uij.fill(0.0);
        Eigen::VectorXd Vij(nTotalDofs); Vij.fill(0.0);
        Eigen::VectorXd Aij(nTotalDofs); Aij.fill(0.0);
        Eigen::VectorXd dUij(nTotalDofs); dUij.fill(0.0);

        for(unsigned int j = 0; j < nTotalDofs; j++){
            Uij(j) = U(TotalDofs[j]);
            Vij(j) = V(TotalDofs[j]);
            Aij(j) = A(TotalDofs[j]);
        }
                
        //Sets the nodal states.
        Nodes[Tag]->SetDisplacements(Uij);   
        Nodes[Tag]->SetVelocities(Vij);
        Nodes[Tag]->SetAccelerations(Aij);
        Nodes[Tag]->SetIncrementalDisplacements(dUij);
    }

    //Gets all elements from the mesh.
    std::map<unsigned int, std::shared_ptr<Element> > Elements = theMesh->GetElements();

    for(std::map<unsigned int, std::shared_ptr<Element> >::iterator it = Elements.begin(); it != Elements.end(); ++it){
        unsigned int Tag = it->first;

        //Saves the material states for each element.
        Elements[Tag]->CommitState();
    }

    //Gets the vector of reactions for the entire model.
    Eigen::VectorXd R = theIntegrator->ComputeReactionForce(theMesh, k);
    ReducedParallelReaction(R);

    for(std::map<unsigned int, std::shared_ptr<Node> >::iterator it = Nodes.begin(); it != Nodes.end(); ++it){
        unsigned int Tag = it->first;

        //Only if the node is fixed.
        if(Nodes[Tag]->IsFixed()){
            //Gets the associated nodal degree-of-freedom.
            std::vector<int> TotalDofs = Nodes[Tag]->GetTotalDegreeOfFreedom();
            unsigned int nTotalDofs = TotalDofs.size();

            //Creates the nodal/incremental vector state.
            Eigen::VectorXd Rij(nTotalDofs); Rij.fill(0.0);

            for(unsigned int j = 0; j < nTotalDofs; j++)
                Rij(j) = R(TotalDofs[j]);
                
            //Sets the nodal reaction.
            Nodes[Tag]->SetReaction(Rij);  
        }
    }
}
