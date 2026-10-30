  // Exhaustive shadow admission: no grid/negative-cache decision is trusted
  // to decide which car/prop pairs receive a canonical geometry check.
  if(VehicleContacts::enabled && Tyra::vu0SatVerify){
    for(const auto& car:VehicleContacts::cars){const int cs=stepOf[car.object];
      for(int object:bodies){if(VehicleContacts::isCar(object))continue;
        int os=stepOf[object];if(os<0)os=makeStep(object,true);
        const auto& a=steps[cs];const auto& b=steps[os];
        const auto& ba=physBodies[a.slot];const auto& bb=physBodies[b.slot];
        Tyra::ConvexContact contact;bool hit=false;
        if(b.sphere){const RbV centre=rbMatT(a.R1,rbSub(b.x1,a.x1));
          hit=Tyra::sphereConvexContact(centre,b.sphereR,ba.lv,ba.verts,ba.lp,ba.planes,contact);
        }else{auto& ag=transformedShape(a,ba);auto& bg=transformedShape(b,bb);
          Tyra::ConvexView av{a.wv,a.nv,ag.normals,ba.planes,ag.edges,ba.edges};
          Tyra::ConvexView bv{b.wv,b.nv,bg.normals,bb.planes,bg.edges,bb.edges};
          hit=Tyra::scalarMotionContactReference(av,bv,rbSub(a.x1,a.x0),rbSub(b.x1,b.x0),contact,&a.obb,b.box?&b.obb:nullptr);
        }
        ++VehicleContacts::current.referencePairs;
        if(hit){++VehicleContacts::current.referenceHits;bool found=false;
          for(const auto& c:contacts)if((c.a==cs && c.b==os)||(c.a==os && c.b==cs)){found=true;break;}
          if(!found){++VehicleContacts::current.missing;++VehicleContacts::current.errors;}
        }
      }
    }
  }
