#include <iostream>
#include <new>
#include <algorithm>

#include "../datatypes.h"
#include "snapshot_number.h"
#include "subhalo.h"
#include "../gravity/tree.h"

HBTReal SubHalo_t::KineticDistance(const Halo_t &halo, const Snapshot_t &snapshot)
{
  HBTReal dx=PeriodicDistance(halo.ComovingPosition, ComovingPosition);
  HBTReal dv=distance(halo.PhysicalVelocity, PhysicalVelocity);
  HBTReal d=dv+snapshot.Header.Hz*snapshot.Header.ScaleFactor*dx;
  return (d>0?d:-d);
}
struct Energy_t
{
  HBTInt pid;
  float E;
};
inline bool CompEnergy(const Energy_t & a, const Energy_t & b)
{
  return (a.E<b.E);
};
void SubHalo_t::unbind(const Snapshot_t &snapshot)
{
  if(1==Particles.size())
  {
	memcpy(ComovingPosition, snapshot.GetComovingPosition(Particles[0]), sizeof(HBTxyz));
	memcpy(PhysicalVelocity, snapshot.GetPhysicalVelocity(Particles[0]), sizeof(HBTxyz));
	return;
  }
  
  OctTree_t tree;
  tree.Reserve(Particles.size());
  Nbound=Particles.size(); //start from full set
  HBTInt Nlast=Nbound*100; 
  
  vector <Energy_t> Elist(Nbound);
  for(HBTInt i=0;i<Nbound;i++)
	Elist[i].pid=Particles[i];
  assert(Nbound<Nlast*HBTConfig.BoundMassPrecision);
  while(Nbound<Nlast*HBTConfig.BoundMassPrecision)
  {
	Nlast=Nbound;
	tree.Build(Nlast, Particles.data(), snapshot);
	for(HBTInt i=0;i<Nlast;i++)
	{
	  HBTInt pid=Elist[i].pid;
	  Elist[i].E=tree.BindingEnergy(snapshot.GetComovingPosition(pid), snapshot.GetPhysicalVelocity(pid), ComovingPosition, PhysicalVelocity, snapshot.GetParticleMass(pid));
	}
	sort(Elist.begin(), Elist.begin()+Nlast, CompEnergy);
	memcpy(ComovingPosition, snapshot.GetComovingPosition(Elist[0].pid), sizeof(HBTxyz));
	memcpy(PhysicalVelocity, snapshot.GetPhysicalVelocity(Elist[0].pid), sizeof(HBTxyz));
	for(Nbound=0;Nbound<Nlast;Nbound++)
	  if(Elist[Nbound].E>0) break;
	if(Nbound<HBTConfig.MinNumPartOfSub)
	{
	  Nbound=0;
	  break;
	}
  }
  if(Nbound)
  {
	Nlast=Particles.size();
	if(Nlast>Nbound*HBTConfig.SourceSubRelaxFactor) Nlast=Nbound*HBTConfig.SourceSubRelaxFactor;
  }
  else
  {
	Nbound=1;
	Nlast=1;
  }
  for(HBTInt i=0;i<Nlast;i++) Particles[i]=Elist[i].pid;
  Particles.resize(Nlast);
}

void MemberShipTable_t::Init(const HBTInt nhalos, const HBTInt nsubhalos, const float alloc_factor)
{
  RawLists.clear();
  RawLists.resize(nhalos+1);
  Lists.Bind(nhalos, RawLists.data()+1);

  AllMembers.clear();
  AllMembers.reserve(nsubhalos*alloc_factor); //allocate more for seed haloes.
  AllMembers.resize(nsubhalos);
}
void MemberShipTable_t::BindMemberLists()
{
  HBTInt offset=0;
  for(HBTInt i=0;i<RawLists.size();i++)
  {
	RawLists[i].Bind(RawLists[i].size(), &(AllMembers[offset]));
	offset+=RawLists[i].size();
	RawLists[i].Resize(0);
  }
}
void MemberShipTable_t::CountMembers(const SubHaloList_t& SubHalos)
{
  for(HBTInt subid=0;subid<SubHalos.size();subid++)
	Lists[SubHalos[subid].HostHaloId].IncrementSize();
}
void MemberShipTable_t::FillMemberLists(const SubHaloList_t& SubHalos)
{
  for(HBTInt subid=0;subid<SubHalos.size();subid++)
	Lists[SubHalos[subid].HostHaloId].push_back(subid);
}
struct CompareMass_t
{
  const SubHaloList_t * SubHalos;
  CompareMass_t(const SubHaloList_t & subhalos)
  {
	SubHalos=&subhalos;
  }
  bool operator () (const HBTInt & i, const HBTInt & j)
  {
	return (*SubHalos)[i].Nbound>(*SubHalos)[j].Nbound;
  }
};
void MemberShipTable_t::SortMemberLists(const SubHaloList_t & SubHalos)
{
  CompareMass_t compare_mass(SubHalos);
  for(HBTInt i=0;i<RawLists.size();i++)
	std::sort(RawLists[i].data(), RawLists[i].data()+RawLists[i].size(), compare_mass);
}
void MemberShipTable_t::Build(const HBTInt nhalos, const SubHaloList_t & SubHalos)
{
  Init(nhalos, SubHalos.size());
  CountMembers(SubHalos);
  BindMemberLists();
  FillMemberLists(SubHalos);
  SortMemberLists(SubHalos);
//   std::sort(AllMembers.begin(), AllMembers.end(), CompareHostAndMass);
}
/*
inline bool SubHaloSnapshot_t::CompareHostAndMass(const HBTInt& subid_a, const HBTInt& subid_b)
{//ascending in host id, descending in mass inside each host, and put NullHaloId to the beginning.
  SubHalo_t a=SubHalos[subid_a], b=SubHalos[subid_b];
  
  if(a.HostHaloId==b.HostHaloId) return (a.Nbound>b.Nbound);
  
  return (a.HostHaloId<b.HostHaloId); //(a.HostHaloId!=SpecialConst::NullHaloId)&&
}*/
void SubHaloSnapshot_t::AssignHost(const HaloSnapshot_t &halo_snap)
{
  vector<HBTInt> ParticleToHost(SnapshotPointer->GetNumberOfParticles(), SpecialConst::NullHaloId);
  for(int haloid=0;haloid<halo_snap.Halos.size();haloid++)
  {
	for(int i=0;i<halo_snap.Halos[haloid].Particles.size();i++)
	  ParticleToHost[i]=haloid;
  }
  for(HBTInt subid=0;subid<SubHalos.size();subid++)
  {
	//rely on track_particle
	SubHalos[subid].HostHaloId=ParticleToHost[SubHalos[subid].TrackParticleId];
	//alternatives: CoreTrack; Split;
  }
  //alternative: trim particles outside fof
    
  MemberTable.Build(halo_snap.Halos.size(), SubHalos);
}
#define CORE_SIZE_MIN 20
#define CORE_SIZE_FRAC 0.25
void SubHaloSnapshot_t::AverageCoordinates()
{
  for(HBTInt subid=0;subid<SubHalos.size();subid++)
  {
	int coresize=SubHalos[subid].Nbound*CORE_SIZE_FRAC;
	if(coresize<CORE_SIZE_MIN) coresize=CORE_SIZE_MIN;
	if(coresize>SubHalos[subid].Nbound) coresize=SubHalos[subid].Nbound;
	
	SnapshotPointer->AveragePosition(SubHalos[subid].ComovingPosition, SubHalos[subid].Particles.data(), coresize);
	SnapshotPointer->AverageVelocity(SubHalos[subid].PhysicalVelocity, SubHalos[subid].Particles.data(), coresize);
  }
}

#define MAJOR_PROGENITOR_MASS_RATIO 0.67
void SubHaloSnapshot_t::DecideCentrals(const HaloSnapshot_t &halo_snap)
/* to select central subhalo according to KineticDistance, and move each central to the beginning of each list in MemberTable*/
{
  for(HBTInt hostid=0;hostid<halo_snap.Halos.size();hostid++)
  {
	MemberShipTable_t::MemberList_t &List=MemberTable.Lists[hostid];
	if(List.size()>1)
	{
	  int n_major;
	  HBTInt MassLimit=SubHalos[List[0]].Nbound*MAJOR_PROGENITOR_MASS_RATIO;
	  for(n_major=1;n_major<List.size();n_major++)
		if(SubHalos[List[n_major]].Nbound<MassLimit) break;
		if(n_major>1)
		{
		  HBTReal dmin=SubHalos[List[0]].KineticDistance(halo_snap.Halos[hostid], *SnapshotPointer);
		  int icenter=0;
		  for(int i=1;i<n_major;i++)
		  {
			HBTReal d=SubHalos[List[i]].KineticDistance(halo_snap.Halos[hostid], *SnapshotPointer);
			if(dmin>d)
			{
			  dmin=d;
			  icenter=i;
			}
		  }
		  if(icenter)  swap(List[0], List[icenter]);
		}
	}
  }
}

void SubHaloSnapshot_t::FeedCentrals(HaloSnapshot_t& halo_snap)
/* replace centrals with host particles;
 * create a new central if there is none
 * initialize new central with host halo center coordinates
 */
{
  HBTInt Npro=SubHalos.size(),NBirth=0;
  for(HBTInt hostid=0;hostid<halo_snap.Halos.size();hostid++)
	if(MemberTable.Lists[hostid].size()==0)  NBirth++;
	SubHalos.resize(Npro+NBirth);
  for(HBTInt hostid=0;hostid<halo_snap.Halos.size();hostid++)
  {
	if(0==MemberTable.Lists[hostid].size()) //create a new sub
	{
	  SubHalos[Npro].TrackId=SpecialConst::NullTrackId; //means to be assigned
	  SubHalos[Npro].HostHaloId=hostid;
	  SubHalos[Npro].SnapshotIndexOfBirth=SnapshotIndex;
	  MemberTable.AllMembers.push_back(Npro);
	  MemberTable.Lists[hostid].Bind(1, &MemberTable.AllMembers.back());
	  //assign host center to new central
	  copyHBTxyz(SubHalos[Npro].ComovingPosition, halo_snap.Halos[hostid].ComovingPosition); 
	  copyHBTxyz(SubHalos[Npro].PhysicalVelocity, halo_snap.Halos[hostid].PhysicalVelocity);
	  Npro++;
	}
	SubHalos[MemberTable.Lists[hostid][0]].Particles.swap(halo_snap.Halos[hostid].Particles); //reuse the halo particles; now halo_snap should
  }
}

void SubHaloSnapshot_t::RefineParticles()
{//it's more expensive to build an exclusive list. so do inclusive here. 
  //TODO: ensure the inclusive unbinding is stable (contaminating particles from big subhaloes may hurdle the unbinding
  for(HBTInt subid=0;subid<SubHalos.size();subid++)
  {
	SubHalos[subid].unbind(*SnapshotPointer);
  }
}

void SubHaloSnapshot_t::RemoveFakeHalos()
/*remove fake ones and assign trackId to new bound ones*/
{
//TODO
}

