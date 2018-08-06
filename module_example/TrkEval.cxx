/*
 * TrkEval.C
 *
 *  Created on: Oct 29, 2017
 *      Author: yuhw@nmsu.edu
 */


#include "TrkEval.h"

#include <interface_main/SQHit.h>
#include <interface_main/SQHit_v1.h>
#include <interface_main/SQMCHit_v1.h>
#include <interface_main/SQHitMap_v1.h>
#include <interface_main/SQHitVector_v1.h>
#include <interface_main/SQEvent_v1.h>
#include <interface_main/SQRun_v1.h>
#include <interface_main/SQSpill_v1.h>
#include <interface_main/SQSpillMap_v1.h>

#include <ktracker/SRecEvent.h>

#include <geom_svc/GeomSvc.h>

#include <fun4all/Fun4AllReturnCodes.h>
#include <fun4all/PHTFileServer.h>
#include <phool/PHNodeIterator.h>
#include <phool/PHIODataNode.h>
#include <phool/getClass.h>

#include <g4main/PHG4TruthInfoContainer.h>
#include <g4main/PHG4Particle.h>
#include <g4main/PHG4HitDefs.h>


#include <TFile.h>
#include <TTree.h>

#include <cstring>
#include <cmath>
#include <cfloat>
#include <stdexcept>
#include <limits>
#include <tuple>

#include <boost/lexical_cast.hpp>

#define LogDebug(exp)		std::cout<<"DEBUG: "  <<__FILE__<<": "<<__LINE__<<": "<< exp << std::endl
#define LogError(exp)		std::cout<<"ERROR: "  <<__FILE__<<": "<<__LINE__<<": "<< exp << std::endl
#define LogWarning(exp)	    std::cout<<"WARNING: "<<__FILE__<<": "<<__LINE__<<": "<< exp << std::endl

TrkEval::TrkEval(const std::string& name) :
SubsysReco(name),
_hit_container_type("Vector"),
_event(0),
_run_header(nullptr),
_spill_map(nullptr),
_event_header(nullptr),
_hit_map(nullptr),
_hit_vector(nullptr),
_out_name("eval.root")
{
}

int TrkEval::Init(PHCompositeNode* topNode) {
	return Fun4AllReturnCodes::EVENT_OK;
}

int TrkEval::InitRun(PHCompositeNode* topNode) {

	ResetEvalVars();
	InitEvalTree();

	p_geomSvc = GeomSvc::instance();

	int ret = GetNodes(topNode);
	if(ret != Fun4AllReturnCodes::EVENT_OK) return ret;

	return Fun4AllReturnCodes::EVENT_OK;
}

int TrkEval::process_event(PHCompositeNode* topNode) {

	if(Verbosity() >= Fun4AllBase::VERBOSITY_SOME)
		std::cout << "Entering TrkEval::process_event: " << _event << std::endl;

	ResetEvalVars();

	if(_spill_map) {
    auto spill_info = _spill_map->get(_b_spill_id);
    if(spill_info) {
      _b_target_pos = spill_info->get_target_pos();
    } else {
      LogWarning("");
    }
  }

	if(_event_header) {
    _b_event_id = _event_header->get_event_id();
    _b_spill_id = _event_header->get_spill_id();
    _b_run_id   = _event_header->get_run_id();
  }

	std::map<int, int> parID_nhits_dc;
	std::map<int, int> parID_nhits_hodo;
	std::map<int, int> parID_nhits_prop;

	std::map<int, int> hitID_ihit;

  if(_hit_vector) {
    _b_n_hits = 0;
    for(int ihit=0; ihit<_hit_vector->size(); ++ihit) {
    	SQHit *hit = _hit_vector->at(ihit);

    	int hitID = hit->get_hit_id();
    	_b_hit_id[_b_n_hits]         = hitID;
    	hitID_ihit[hitID] = ihit;
      _b_drift_distance[_b_n_hits] = hit->get_drift_distance();
      _b_pos[_b_n_hits]            = hit->get_pos();
      _b_detector_z[_b_n_hits]     = p_geomSvc->getPlanePosition(hit->get_detector_id());
      _b_detector_id[_b_n_hits]    = hit->get_detector_id();

      if(_truth) {
      	int track_id = hit->get_track_id();

      	if(hit->get_detector_id() >= 1 and hit->get_detector_id() <=30) {
        	if(parID_nhits_dc.find(track_id)!=parID_nhits_dc.end())
        		parID_nhits_dc[track_id] = parID_nhits_dc[track_id]+1;
        	else
        		parID_nhits_dc[track_id] = 1;
      	}
      	if(hit->get_detector_id() >= 31 and hit->get_detector_id() <=46) {
        	if(parID_nhits_hodo.find(track_id)!=parID_nhits_hodo.end())
        		parID_nhits_hodo[track_id] = parID_nhits_hodo[track_id]+1;
        	else
        		parID_nhits_hodo[track_id] = 1;
      	}
      	if(hit->get_detector_id() >= 47 and hit->get_detector_id() <=54) {
        	if(parID_nhits_prop.find(track_id)!=parID_nhits_prop.end())
        		parID_nhits_prop[track_id] = parID_nhits_prop[track_id]+1;
        	else
        		parID_nhits_prop[track_id] = 1;
      	}


      	_b_truth_x[_b_n_hits] = hit->get_truth_x();
      	_b_truth_y[_b_n_hits] = hit->get_truth_y();
      	_b_truth_z[_b_n_hits] = hit->get_truth_z();

      	double uVec[3] = {
      			p_geomSvc->getCostheta(hit->get_detector_id()),
						p_geomSvc->getSintheta(hit->get_detector_id()),
						0
      	};
      	_b_truth_pos[_b_n_hits] =
      			_b_truth_x[_b_n_hits]*uVec[0] + _b_truth_y[_b_n_hits]*uVec[1];

      	//LogDebug("detector_id: " << _b_detector_id[_b_n_hits]);
      }
      ++_b_n_hits;
    }
  }

  typedef std::tuple<int, int> ParRecoPair;
  std::map<ParRecoPair, int> parID_recID_nHit;

  typedef std::tuple<int, int> TrkIDNHit;
  std::map<int, TrkIDNHit> parID_bestRecID;
  std::map<int, TrkIDNHit> recID_bestParID;

  if(_recEvent) {
  	for(int itrack=0; itrack<_recEvent->getNTracks(); ++itrack){
  		SRecTrack recTrack = _recEvent->getTrack(itrack);
  		for(int ihit=0; ihit<recTrack.getNHits();++ihit) {
  			int hitID = recTrack.getHitIndex(ihit);
  			SQHit *hit = _hit_vector->at(hitID_ihit[hitID]);
  			int parID = hit->get_track_id();
  			ParRecoPair key = std::make_tuple(parID, itrack);

      	if(parID_recID_nHit.find(key)!=parID_recID_nHit.end())
      		parID_recID_nHit[key] = parID_recID_nHit[key]+1;
      	else
      		parID_recID_nHit[key] = 1;
  		}
  	}

  	for(auto iter=parID_recID_nHit.begin();
  			iter!=parID_recID_nHit.end(); ++iter) {
  		int parID = std::get<0>(iter->first);
  		int recID = std::get<1>(iter->first);
  		int nHit  = iter->second;

    	if(parID_bestRecID.find(parID)!=parID_bestRecID.end()) {
    		int nHit_current_best  = std::get<1>(parID_bestRecID[parID]);
    		if (nHit > nHit_current_best)
    			parID_bestRecID[parID] = std::make_tuple(recID, nHit);
    	}
    	else
    		parID_bestRecID[parID] = std::make_tuple(recID, nHit);

    	if(recID_bestParID.find(recID)!=recID_bestParID.end()) {
    		int nHit_current_best  = std::get<1>(recID_bestParID[recID]);
    		if (nHit > nHit_current_best)
    			recID_bestParID[recID] = std::make_tuple(parID, nHit);
    	}
    	else
    		recID_bestParID[recID] = std::make_tuple(parID, nHit);
  	}


  }

  if(_truth) {
  	for(auto iter=_truth->GetPrimaryParticleRange().first;
  			iter!=_truth->GetPrimaryParticleRange().second;
  			++iter) {
  		PHG4Particle * par = iter->second;
  		TVector3 mom(par->get_px(), par->get_py(), par->get_pz());
  		gpx[n_particles] = par->get_px();
  		gpy[n_particles] = par->get_py();
  		gpz[n_particles] = par->get_pz();
  		gpt[n_particles] = mom.Pt();
  		geta[n_particles] = mom.Eta();
  		gphi[n_particles] = mom.Phi();

  		int parID = par->get_track_id();
  		gnhits[n_particles] =
  				parID_nhits_dc[parID] +
					parID_nhits_hodo[parID] +
					parID_nhits_prop[parID];

  		gndc[n_particles] = parID_nhits_dc[parID];
  		gnhodo[n_particles] = parID_nhits_hodo[parID];
  		gnprop[n_particles] = parID_nhits_prop[parID];

  		ntruhits[n_particles] = 0;
  		if(parID_bestRecID.find(parID)!=parID_bestRecID.end()) {
  			ntruhits[n_particles] = std::get<1>(parID_bestRecID[parID]);
  		}

  		++n_particles;
  	}
  }

  //LogDebug("nhits: " << _hit_vector->size());

  _tout->Fill();

  if(Verbosity() >= Fun4AllBase::VERBOSITY_SOME)
    std::cout << "Leaving TrkEval::process_event: " << _event << std::endl;
  ++_event;

  return Fun4AllReturnCodes::EVENT_OK;
}

int TrkEval::End(PHCompositeNode* topNode) {
  if(Verbosity() >= Fun4AllBase::VERBOSITY_SOME)
    std::cout << "TrkEval::End" << std::endl;

  PHTFileServer::get().cd(_out_name.c_str());
  _tout->Write();

  return Fun4AllReturnCodes::EVENT_OK;
}

int TrkEval::InitEvalTree() {
  PHTFileServer::get().open(_out_name.c_str(), "RECREATE");

  _tout = new TTree("T", "TrkEval");
  _tout->Branch("runID",         &_b_run_id,          "runID/I");
  _tout->Branch("spillID",       &_b_spill_id,        "spillID/I");
  _tout->Branch("liveProton",    &_b_target_pos,      "liveProton/F");
  _tout->Branch("eventID",       &_b_event_id,        "eventID/I");

  _tout->Branch("nHits",         &_b_n_hits,          "nHits/I");
  _tout->Branch("hitID",         _b_hit_id,           "hitID[nHits]/I");
  _tout->Branch("detectorID",    _b_detector_id,      "detectorID[nHits]/I");
  _tout->Branch("detectorZ",     _b_detector_z,       "detectorZ[nHits]/F");
  _tout->Branch("truth_x",       _b_truth_x,          "truth_x[nHits]/F");
  _tout->Branch("truth_y",       _b_truth_y,          "truth_y[nHits]/F");
  _tout->Branch("truth_z",       _b_truth_z,          "truth_z[nHits]/F");
  _tout->Branch("truth_pos",     _b_truth_pos,        "truth_pos[nHits]/F");
  _tout->Branch("driftDistance", _b_drift_distance,   "driftDistance[nHits]/F");
  _tout->Branch("pos",           _b_pos,              "pos[nHits]/F");

  _tout->Branch("n_particles",   &n_particles,        "n_particles/I");
  _tout->Branch("gpx",           gpx,                 "gpx[n_particles]/F");
  _tout->Branch("gpy",           gpy,                 "gpy[n_particles]/F");
  _tout->Branch("gpz",           gpz,                 "gpz[n_particles]/F");
  _tout->Branch("gpt",           gpt,                 "gpt[n_particles]/F");
  _tout->Branch("geta",          geta,                "geta[n_particles]/F");
  _tout->Branch("gphi",          gphi,                "gphi[n_particles]/F");
  _tout->Branch("gnhits",        gnhits,              "gnhits[n_particles]/I");
  _tout->Branch("gndc",          gndc,                "gndc[n_particles]/I");
  _tout->Branch("gnhodo",        gnhodo,              "gnhodo[n_particles]/I");
  _tout->Branch("gnprop",        gnprop,              "gnprop[n_particles]/I");

  _tout->Branch("ntruhits",      ntruhits,            "ntruhits[n_particles]/I");

  return 0;
}

int TrkEval::ResetEvalVars() {
  _b_run_id = std::numeric_limits<int>::max();
  _b_spill_id = std::numeric_limits<int>::max();
  _b_target_pos = std::numeric_limits<float>::max();
  _b_event_id = std::numeric_limits<int>::max();

  _b_n_hits = 0;
  for(int i=0; i<10000; ++i) {
    _b_detector_id[i]    = std::numeric_limits<short>::max();
    _b_drift_distance[i] = std::numeric_limits<float>::max();
    _b_pos[i]            = std::numeric_limits<float>::max();
    _b_detector_z[i]     = std::numeric_limits<float>::max();

    _b_truth_x[i]       = std::numeric_limits<float>::max();
    _b_truth_y[i]       = std::numeric_limits<float>::max();
    _b_truth_z[i]       = std::numeric_limits<float>::max();
    _b_truth_pos[i]     = std::numeric_limits<float>::max();
  }

  n_particles = 0;
  for(int i=0; i<1000; ++i) {
    gpx[i]        = std::numeric_limits<float>::max();
    gpy[i]        = std::numeric_limits<float>::max();
    gpz[i]        = std::numeric_limits<float>::max();
    gpt[i]        = std::numeric_limits<float>::max();
    geta[i]       = std::numeric_limits<float>::max();
    gphi[i]       = std::numeric_limits<float>::max();
    gnhits[i]     = std::numeric_limits<int>::max();
    gndc[i]       = std::numeric_limits<int>::max();
    gnhodo[i]     = std::numeric_limits<int>::max();
    gnprop[i]     = std::numeric_limits<int>::max();
    ntruhits[i]   = std::numeric_limits<int>::max();
  }

  return 0;
}

int TrkEval::GetNodes(PHCompositeNode* topNode) {

  _run_header = findNode::getClass<SQRun>(topNode, "SQRun");
  if (!_run_header) {
    LogError("!_run_header");
    //return Fun4AllReturnCodes::ABORTEVENT;
  }

  _spill_map = findNode::getClass<SQSpillMap>(topNode, "SQSpillMap");
  if (!_spill_map) {
    LogError("!_spill_map");
    //return Fun4AllReturnCodes::ABORTEVENT;
  }

  _event_header = findNode::getClass<SQEvent>(topNode, "SQEvent");
  if (!_event_header) {
    LogError("!_event_header");
    //return Fun4AllReturnCodes::ABORTEVENT;
  }

  if(_hit_container_type.find("Map") != std::string::npos) {
    _hit_map = findNode::getClass<SQHitMap>(topNode, "SQHitMap");
    if (!_hit_map) {
      LogError("!_hit_map");
      return Fun4AllReturnCodes::ABORTEVENT;
    }
  }

  if(_hit_container_type.find("Vector") != std::string::npos) {
    _hit_vector = findNode::getClass<SQHitVector>(topNode, "SQHitVector");
    if (!_hit_vector) {
      LogError("!_hit_vector");
      return Fun4AllReturnCodes::ABORTEVENT;
    }
  }

  _truth = findNode::getClass<PHG4TruthInfoContainer>(topNode, "G4TruthInfo");
  if (!_truth) {
    LogError("!_truth");
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  _recEvent = findNode::getClass<SRecEvent>(topNode, "SRecEvent");
  if (!_recEvent) {
    LogError("!_recEvent");
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  return Fun4AllReturnCodes::EVENT_OK;
}







