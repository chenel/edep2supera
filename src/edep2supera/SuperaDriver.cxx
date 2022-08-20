

#include "SuperaDriver.h"
#include <iostream>
#include <map>

namespace edep2supera {
	
	void SuperaDriver::Configure(const std::string& name, const std::map<std::string,std::string>& params)
    {
        if(name == "edep2supera") {
            supera::PSet cfg;
            cfg.data = params;
			Configure(cfg);
			
			// if (cfg.exists("LogLevel") > 0) SetLogConfig(Logger::parseStringThresh(cfg.get<std::string>("LogLevel")));
        }
        else{
            std::string msg = name + " is not known to Supera...";
            throw supera::meatloaf(msg);
        }

    }

	void SuperaDriver::Configure(const supera::PSet &cfg)
	{
		if (cfg.exists("ActiveDetectors") > 0) allowed_detectors = cfg.get<std::vector<std::string>>("ActiveDetectors");
	}

	void SuperaDriver::ExpandBBox(supera::EventInput &result)
	{

		supera::Point3D botleft = supera::Point3D(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
		supera::Point3D topright = supera::Point3D(-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max());
		for (auto &PI : result)
		{
			const double epsilon = 1.e-3;
			botleft.x = std::min(PI.edep_bottom_left.x - epsilon, botleft.x);
			botleft.y = std::min(PI.edep_bottom_left.y - epsilon, botleft.y);
			botleft.z = std::min(PI.edep_bottom_left.z - epsilon, botleft.z);

			topright.x = std::max(PI.edep_top_right.x + epsilon, topright.x);
			topright.y = std::max(PI.edep_top_right.y + epsilon, topright.y);
			topright.z = std::max(PI.edep_top_right.z + epsilon, topright.z);

		}
		for (auto &PI : result)
		{
			PI.edep_bottom_left=botleft;
			PI.edep_top_right=topright;
		}
	}




	supera::EventInput SuperaDriver::ReadEvent(const TG4Event *ev) // returns a supera.Event to be used in SuperaAtomic
	{
		supera::EventInput result;

		std::map<supera::TrackID_t, supera::InstanceID_t> tid_to_res; // map from track_id to result vector index

		for (auto const &traj : ev->Trajectories)
		{
			supera::ParticleInput part_input;

			part_input.valid   = true;
			part_input.part    = this->TG4TrajectoryToParticle(traj);
			part_input.part.id = result.size();
			part_input.type    = this->InferProcessType(traj);

			tid_to_res[traj.GetTrackId()]=part_input.part.id;

			result.push_back(part_input);

		}
		//supera::ParticleIndex store;
		//store.SetParentInfo(result);
		//mcparticletree 


		// for (auto const &traj : ev->Trajectories)
		// {
		// 	InstanceID_t resid=tid_to_res[traj.GetTrackId()];
		// 	TrackID_t par_tid = result[resid].part.parent_trackid;
		// 	InstanceID_t par_resid=tid_to_res[par_tid];


		// 	result[resid].part.parent_pdg=result[par_resid].part.pdg;
		// 	result[resid].part.parent_process=result[par_resid].part.process;
		// 	result[resid].part.parent_vtx=result[par_resid].part.vtx;

		// 	if (std::count(result[par_resid].part.children_id.begin(), result[par_resid].part.children_id.end(), resid)==0) 
		// 	{
		// 		result[par_resid].part.children_id.push_back(resid);
		// 	}

		// 	InstanceID_t store=resid;
		// 	while (result[store].part.parent_trackid != 0 && result[store].part.parent_trackid !=kINVALID_TRACKID)// && result[store].part.parent_trackid != -1)
		// 	{
		// 		store = tid_to_res[result[store].part.parent_trackid];
		// 	}
			

		// 	result[resid].part.ancestor_vtx = result[store].part.parent_vtx;
		// 	result[resid].part.ancestor_pdg = result[store].part.parent_pdg;
		// 	result[resid].part.ancestor_process = result[store].part.parent_process;
		// }

	
		for (auto const &det : ev->SegmentDetectors)
		{
			if (ev->SegmentDetectors.size() > 0)
			{
				std::cout << "Accepting list of active regions from config"<< std::endl;
				if (std::find(allowed_detectors.begin(), allowed_detectors.end(), det.first) == allowed_detectors.end())
				{
					std::cout << det.first<< " not in acceptable active regions"<< std::endl;
					break;
				}

			}
			for (auto const &dep : det.second)
			{

				for (auto const &tid : dep.Contrib)
				{
					auto const& start = dep.GetStart();
					auto const& end   = dep.GetStop();

					supera::InstanceID_t resid=tid_to_res[tid];

					BBox_bounds(dep, result[resid]);

					if (result[resid].part.energy_deposit==0)
					{
						result[resid].part.energy_deposit+=dep.GetEnergyDeposit();
						result[resid].part.first_step=supera::Vertex(start.X() / 10., start.Y() / 10., start.Z() / 10., start.T()) ;
						result[resid].part.last_step=supera::Vertex(end.X() / 10., end.Y() / 10., end.Z() / 10., end.T());	
					}
						
					else if (result[resid].part.energy_deposit>0)
						{
						result[resid].part.energy_deposit+=dep.GetEnergyDeposit();
						if (start.T()<result[resid].part.vtx.time) result[resid].part.first_step=supera::Vertex(start.X() / 10., start.Y() / 10., start.Z() / 10., start.T()) ;
						if (result[resid].part.end_pt.time<end.T()) result[resid].part.last_step=supera::Vertex(end.X() / 10., end.Y() / 10., end.Z() / 10., end.T());	
						}
					else LOG.ERROR() <<"energy deposition logic makes no sense" <<"\n";

					if (dep.Contrib.size() > 1)
					{	
						LOG.ERROR() <<"multiple contributors to energy depositions, set /edep/hitSeparation<=0"<<"\n";
						break;
					}
				}
				
			}
		}
		ExpandBBox(result);
		return result;
	}

	void SuperaDriver::VoxelizeEvent(const TG4Event *ev, const supera::ImageMeta3D &meta, supera::EventInput &result)
	{
		LOG.DEBUG() << "starting voxelization"<< "\n";
		std::map<supera::TrackID_t, supera::InstanceID_t> tid_to_res; // map from track_id to result vector index
		for (auto const &PI : result) tid_to_res[PI.part.trackid] = PI.part.id;
		for (auto const &det : ev->SegmentDetectors)
		{
			if (ev->SegmentDetectors.size() > 0)
			{
				std::cout << "Accepting list of active regions from config"<< std::endl;
				if (std::find(allowed_detectors.begin(), allowed_detectors.end(), det.first) == allowed_detectors.end())
				{
					std::cout << det.first<< "not in acceptable active regions"<< std::endl;
					break;
				}

			}
			for (auto const &dep : det.second)
			{
				for (auto const &tid : dep.Contrib)
				{
					// auto const& start = dep.GetStart();
					// auto const& end   = dep.GetStop();
					supera::InstanceID_t resid = tid_to_res[tid];
					std::vector<supera::EDep> myedeps = MakeEDeps(dep, meta, result[resid].part.dist_travel);
					// if (dep.GetEnergyDeposit() > 0)
					// {
					// 	std::cout << dep.GetStart().X() / 10. << " " << dep.GetStart().Y() / 10. << " " << dep.GetStart().Z() / 10. << " " << dep.GetEnergyDeposit() << " dep start xyz,e" << std::endl;
					// 	std::cout << dep.GetStop().X() / 10. << " " << dep.GetStop().Y() / 10. << " " << dep.GetStop().Z() / 10. << " " << dep.GetEnergyDeposit() << " dep stop xyz,e" << std::endl;
					// 	std::cout << myedeps[0].x << " " << myedeps[0].y << " " << myedeps[0].z << std::endl;
					// 	std::cout << myedeps[0].e << " " << myedeps[0].dedx << " edep x,y,z,e,dedx" << std::endl;
					// }
					if (result[resid].pcloud.size() == 0) result[resid].pcloud = myedeps;
					else result[resid].pcloud.insert(result[resid].pcloud.end(), myedeps.begin(), myedeps.end());

					if (dep.Contrib.size() > 1)
					{
						LOG.ERROR() << "multiple contributors to energy depositions, set /edep/hitSeparation<=0"<< "\n";
						break;
					}
				}
			}
		}
		for (auto &PI : result)
		{
			std::cout << PI.pcloud.size() << " pcloud size "<<PI.part.energy_deposit<< " energy deposit " <<PI.part.trackid<< " trackid "<<PI.part.parent_trackid<< " parent_trackid"<< std::endl;
			std::cout <<"           "<< PI.part.first_step.pos.x << " " << PI.part.first_step.pos.y << " " << PI.part.first_step.pos.z << " first step pos " << std::endl;
			std::cout <<"           "<< PI.part.last_step.pos.x << " " << PI.part.last_step.pos.y << " " << PI.part.last_step.pos.z << " last step pos " << std::endl;
			std::cout << "           " << PI.edep_bottom_left.x << " " << PI.edep_bottom_left.y << " " << PI.edep_bottom_left.z << " edep_bottom_left " << std::endl;
			std::cout << "           " << PI.edep_top_right.x << " " << PI.edep_top_right.y << " " << PI.edep_top_right.z << " edep_top_right " << std::endl;
			std::cout << "    " << PI.part.id << " id " << PI.part.pdg << " pdg " << PI.part.energy_init << " energy_init " << PI.part.dist_travel << " dist_travel????????" << std::endl;
			std::cout << "    " << PI.part.vtx.pos.x << " " << PI.part.vtx.pos.y << " " << PI.part.vtx.pos.z << " " << PI.part.vtx.time<< " vtx " << std::endl;
			std::cout << "    " << PI.part.end_pt.pos.x << " " << PI.part.end_pt.pos.y << " " << PI.part.end_pt.pos.z << " " << PI.part.end_pt.time << " end_pt " << std::endl;
			std::cout << "    " << PI.part.px << " " << PI.part.py << " " << PI.part.pz << " " << " momentum " << std::endl;
		}

	}

	supera::Particle SuperaDriver::TG4TrajectoryToParticle(const TG4Trajectory& edepsim_part)
	{
		supera::Particle result;
		auto const& start = edepsim_part.Points.front().GetPosition();
		auto const& end   = edepsim_part.Points.back().GetPosition();

		// first, last steps, distance travel, energy_deposit, num_voxels

		result.trackid = edepsim_part.GetTrackId(); ///< Geant4 track id
		result.pdg = edepsim_part.GetPDGCode();	 ///< PDG code
		result.px = edepsim_part.GetInitialMomentum().Px();
		result.py = edepsim_part.GetInitialMomentum().Py();
		result.pz = edepsim_part.GetInitialMomentum().Pz();										///< (x,y,z) component of particle's initial momentum
		result.vtx = supera::Vertex(start.X() / 10., start.Y() / 10., start.Z() / 10., start.T()); ///< (x,y,z,t) of particle's vertex information
		result.end_pt = supera::Vertex(end.X() / 10., end.Y() / 10., end.Z() / 10., end.T());		///< (x,y,z,t) at which particle disappeared from G4WorldVolume
		//result.process = edepsim_part.Points[0].GetProcess(); 										///< string identifier of the particle's creation process from Geant4
		result.energy_init = edepsim_part.GetInitialMomentum().E();										///< initial energy of the particle
		result.parent_trackid = edepsim_part.GetParentId();												///< PDG code of the parent particle


		return result;
	}

	void SuperaDriver::BBox_bounds(const TG4HitSegment &deposition, supera::ParticleInput &PI)
	{
        if (deposition.GetEnergyDeposit()>0)
        {
        //   std::cout << p.trackid << " " << p.energy_deposit << " adding first " << p.first_step.pos.x << " " << p.first_step.pos.y << " " << p.first_step.pos.z << std::endl;
        //   std::cout << p.trackid << " " << p.energy_deposit << " adding last " << p.last_step.pos.x << " " << p.last_step.pos.y << " " << p.last_step.pos.z << std::endl;
		const double epsilon = 1.e-3;
		PI.edep_bottom_left.x = std::min(deposition.GetStart().X() / 10. - epsilon, PI.edep_bottom_left.x);
		PI.edep_bottom_left.y = std::min(deposition.GetStart().Y() / 10. - epsilon, PI.edep_bottom_left.y);
		PI.edep_bottom_left.z = std::min(deposition.GetStart().Z() / 10. - epsilon, PI.edep_bottom_left.z);
		PI.edep_bottom_left.x = std::min(deposition.GetStop().X() / 10. - epsilon, PI.edep_bottom_left.x);
		PI.edep_bottom_left.y = std::min(deposition.GetStop().Y() / 10. - epsilon, PI.edep_bottom_left.y);
		PI.edep_bottom_left.z = std::min(deposition.GetStop().Z() / 10. - epsilon, PI.edep_bottom_left.z);

		PI.edep_top_right.x = std::max(deposition.GetStart().X() / 10. + epsilon, PI.edep_top_right.x);
		PI.edep_top_right.y = std::max(deposition.GetStart().Y() / 10. + epsilon, PI.edep_top_right.y);
		PI.edep_top_right.z = std::max(deposition.GetStart().Z() / 10. + epsilon, PI.edep_top_right.z);
		PI.edep_top_right.x = std::max(deposition.GetStop().X() / 10. + epsilon, PI.edep_top_right.x);
		PI.edep_top_right.y = std::max(deposition.GetStop().Y() / 10. + epsilon, PI.edep_top_right.y);
		PI.edep_top_right.z = std::max(deposition.GetStop().Z() / 10. + epsilon, PI.edep_top_right.z);
		}
	}

	supera::ProcessType SuperaDriver::InferProcessType(const TG4Trajectory& edepsim_part)
	{
		auto pdg_code = edepsim_part.GetPDGCode();
		auto g4type_main = edepsim_part.Points.front().GetProcess();
		auto g4type_sub  = edepsim_part.Points.front().GetSubprocess();


		if(pdg_code == 22) return supera::kPhoton;
		else if(std::abs(pdg_code) == 11) 
		{
			std::cout << "PDG " << pdg_code << " G4ProcessType " << g4type_main 
			<< " SubProcessType " << g4type_sub
			<< std::endl;
			/*
			if (g4type_sub==2) return kIonization;
			if (g4type_sub==13) return kPhotoElectron;
			if (g4type_sub==13) return kCompton;
			if (g4type_sub==14) return kConversion;




			std::string prc = mcpart.Process();
			if( prc == "muIoni" || prc == "hIoni" || prc == "muPairProd" )
				grp.type = kDelta;
			else if( prc == "muMinusCaptureAtRest" || prc == "muPlusCaptureAtRest" || prc == "Decay" )
				grp.type = kDecay;
			else if( prc == "compt"  )
				grp.type = kCompton;
			else if( prc == "phot"   )
				grp.type = kPhotoElectron;
			else if( prc == "eIoni"  )
				grp.type = kIonization;
			else if( prc == "conv"   )
				grp.type = kConversion;
			else if( prc == "primary")
				grp.type = kPrimary;
			else
				grp.type = kOtherShower;
			*/

			return supera::kOtherShower;			
		}
		else if(pdg_code == 2112) return supera::kNeutron;
		else 
		{
			
			return supera::kTrack;
		}
	}
}
