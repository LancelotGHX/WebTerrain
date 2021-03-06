/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <memory>
#include <string>
#include <regex>

#include <grpcpp/grpcpp.h>
#include "geodesic.grpc.pb.h"

#include<sstream>
#include<unistd.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <fstream>
#include <sstream>
#include <sys/times.h>

#include "distance.h"
#include "geodesic_algorithm_subdivision.h"
#include "critical.h"

using namespace geodesic_gRPC;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

//TODO: only valid for wsl environment, make it to be read from config file
std::string src_dir = "/mnt/c/Users/huaxuan/source/repos/terrain_web/node/threejs/src/";

geodesic::SurfacePoint findVertexByCordStr(std::string cordStr, geodesic::Mesh* mesh){
  std::string delimiter = ",";
  size_t pos = 0;
  std::string token;
  std::vector<_Float32> cords;
  geodesic::SurfacePoint result;

  while ((pos = cordStr.find(delimiter)) != std::string::npos) {
      token = cordStr.substr(0, pos);
      std::cout << token << std::endl;
      cordStr.erase(0, pos + delimiter.length());
      cords.push_back(std::stof(token));
  }

  for(int i=0; i<mesh->vertices().size();i++){
      geodesic::SurfacePoint v(&mesh->vertices()[i]);
      if(v.x()==cords[0] && v.y()==cords[1]){
          std::cout<<"found match vertex"<<std::endl;        
          result = v;
      }
  }
  return result;
}

std::string generateTerrainInfoJSON(geodesic::Mesh mesh){
  std::string json = "";
  json += "{";
  json += "\"vertices\": " + std::to_string(mesh.vertices().size()) + ",";
  json += "\"edges\": " + std::to_string(mesh.edges().size())+ ",";
  json += "\"faces\": " + std::to_string(mesh.faces().size())+ ",";

  double minx = 1e100;
  double maxx = -1e100;
  double miny = 1e100;
  double maxy = -1e100;
  double minz = 1e100;
  double maxz = -1e100;
  for(unsigned i=0; i<mesh.vertices().size(); ++i)
  {
    geodesic::Vertex& v = mesh.vertices()[i];
    minx = std::min(minx, v.x());		
    maxx = std::max(maxx, v.x());
    miny = std::min(miny, v.y());
    maxy = std::max(maxy, v.y());
    minz = std::min(minz, v.z());
    maxz = std::max(maxz, v.z());
  }

  json += "\"minx\": " + std::to_string(minx) + ",";
  json += "\"miny\": " + std::to_string(miny) + ",";
  json += "\"minz\": " + std::to_string(minz) + ",";
  json += "\"maxx\": " + std::to_string(maxx) + ",";
  json += "\"maxy\": " + std::to_string(maxy) + ",";
  json += "\"maxz\": " + std::to_string(maxz);
  json += "}";
  
  return json;
}

// Logic and data behind the server's behavior.
class GeodesicServiceImpl final : public Geodesic::Service {

  public:
    geodesic::Mesh mesh;
    
    GeodesicServiceImpl(){
      // Init();   
    };

    ~GeodesicServiceImpl(){};

    bool LoadModelByPath(std::string path){
      //TODO dynamic load model path
      // std::vector<double> points;	
      // std::vector<unsigned> faces;
      char* path_c = strcpy(new char[path.length() + 1], path.c_str());
      bool success = geodesic::read_mesh_from_file(path_c, points,faces);
      if(!success)
      {
        // TODO::handle error
        std::cout<<"Load model failed!"<<std::endl;
        return false;
      }
      mesh.initialize_mesh_data(points, faces);
      return true;
    }

  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    std::string prefix("Hello ");
    reply->set_message(prefix + request->name());
    return Status::OK;
  }
  Status FindPathByVertexCord(ServerContext* context, const FindPathByVertexCordRequest* request,
                       Path* reply) override {
    geodesic::GeodesicAlgorithmExact algorithm(&mesh);
    geodesic::SurfacePoint source = findVertexByCordStr(request->v1(), &mesh);
    geodesic::SurfacePoint destination = findVertexByCordStr(request->v2(), &mesh);

    std::vector<geodesic::SurfacePoint> path;
    algorithm.geodesic(source, destination, path);

    for(unsigned i = 0; i<path.size(); ++i)
    {
        geodesic::SurfacePoint& s = path[i];        
        // std::cout << s.x() << "\t" << s.y() << "\t" << s.z() << std::endl;
        reply->add_path(s.x());
        reply->add_path(s.y());
        reply->add_path(s.z());
    }
    reply->set_message("success");
    return Status::OK;
  }

  Status LoadModel(ServerContext* context, const ModelPath* request,
                       TerrainInfo* reply) override 
  {
    if(LoadModelByPath(src_dir + request->model_path())){
      std::string json = generateTerrainInfoJSON(mesh);
      std::cout << json << std::endl;
      reply->set_info(json);
      return Status::OK;
    }
    return Status::CANCELLED;
  }

  Status SimplifyTerrain(ServerContext* context, const SimplifyTerrainRequest* request,
                       ModelPath* reply) override {
    std::cout<<"Simplify terrain"<<std::endl;
    geodesic::GeodesicAlgorithmExact algorithm(&mesh);
    float beta = request->beta();
    std::string old_model_path = src_dir + request->old_model_path();
    // A FUNCTION TO SIMPLIFY
    bool graph_success = simplify::generate_graph(old_model_path, old_model_path+".graph");
    if(graph_success){
      std::string graph_path = old_model_path.append(".graph");
      std::string off_path = std::regex_replace(graph_path, std::regex(".off.graph"), std::to_string(beta)) + ".off";
      bool off_success = simplify::generateOff(graph_path, beta, off_path);
      if(off_success){        
        std::cout<<off_path<<std::endl;
        reply->set_model_path(std::regex_replace(off_path, std::regex(src_dir), std::string("")));
        return Status::OK;
      }
    }
    return Status::CANCELLED;
  }

  Status UploadModelContent(ServerContext* context, const ModelContent* request,
                       ModelPath* reply) override {    
    std::cout<<"upload content"<<std::endl;
    std::ofstream outfile;
    // TODO: assume upload off file only, later need to do format conversion from obj to off, etc
    outfile.open(src_dir+"models/off/upload.off");
    outfile << request->content();
    outfile.close();
    reply->set_model_path("models/off/upload.off");
    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  GeodesicServiceImpl service;

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();
  return 0;
}
