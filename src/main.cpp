/* sources used for completing this project:

project walkthrough and Q&A: https://www.youtube.com/watch?v=7sI3VHFPP0w
spline tool: http://kluge.in-chemnitz.de/opensource/spline/

*/

#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"                     // spline tool

using namespace std;

// for convenience
using json = nlohmann::json;

// for converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// checks if the SocketIO event has JSON data.
// if there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

// calculate distance between two points
double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}

// closest waypoint in map next to point (x,y)
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

// next waypoint in positive s-direction in map next to point (x,y)
int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

int main() {
  uWS::Hub h;

  // load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // the max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  // load map data
  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

  // car starts in middle lane
  int lane = 1;

  // variable for lc_alg
  bool lc_alg = false;

  // reference velocity to target (start with 0 mph)
  double ref_vel = 0; // in mph

  h.onMessage([&ref_vel,&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy,&lane, &lc_alg](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);

        string event = j[0].get<string>();

        if (event == "telemetry") {
          // j[1] is the data JSON object

        	// main car's localization data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

            // std::cout << "car_x " << car_x << endl;
            // std::cout << "car_y " << car_y << endl;
            // std::cout << "car_yaw " << car_yaw << endl;
            // std::cout << "car_s " << car_s << endl;
            // std::cout << "car_d " << car_d << endl;

          	// previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];

            // size of previous path
            int prev_size = previous_path_x.size();

          	// previous path's end s and d values
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// sensor fusion data
            // a list of all other cars on the same side of the road
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

            if(prev_size > 0)
            {
                car_s = end_path_s;
            }

            // bool variable for checking, if car in front is too close
            bool too_close = false;

            // bool variables to enable lane changes
            bool change_left = false;
            bool change_right = false;

            // lists to store indices of cars in lanes left or right of the car
            vector<int> leftcars;
            vector<int> rightcars;

            // check for cars ahead
            for (int i = 0; i < sensor_fusion.size(); i++)
            {
                // car is in my lane
                float d = sensor_fusion[i][6];
                if(d < (4. * (lane+1)) && d > (4. * lane))
                {
                    double vx = sensor_fusion[i][3];
                    double vy = sensor_fusion[i][4];
                    double check_speed = sqrt(vx*vx + vy*vy);
                    double check_car_s = sensor_fusion[i][5];

                    // if using previos points can project s value outward some time
                    check_car_s += ((double)prev_size * 0.02 * check_speed);

                    // check s values greater than mine and s gap
                    if ((check_car_s > car_s) && (check_car_s - car_s) < 30.)
                    {

                        // enable algorithm to check for lane changes
                        lc_alg = true;

                        // set bool flag too close
                        too_close = true;

                        // lower target speed dependent on distance and speed difference
                        if((check_car_s - car_s) < 10.)
                        {
                            ref_vel -= .224;
                        }

                        else if(ref_vel > (check_speed - 3.))
                        {
                            ref_vel -= .224 * (abs(ref_vel - check_speed) / ref_vel) * (15 / (check_car_s - car_s));
                        }

                        else if(abs(ref_vel - check_speed) <= 3.)
                        {
                            ref_vel -= (ref_vel - check_speed) / check_speed * .224;
                        }

                        std::cout << "Car in front of us is too close: Lowering speed. Target speed: ";
                        std::cout <<  ref_vel << endl;

                    }
                }

                // store car number for cars that are not in my lane, but in the lane right of me
                else if (d < (4. * (lane+2)) && d > (4. * (lane+1)))
                {
                    rightcars.push_back(i);
                }

                // store car number for cars that are not in my lane, but in the lane left of me
                else if (d < (4. * lane) && d > (4. * (lane-1)))
                {
                    leftcars.push_back(i);
                }

            }

            // lane change algorithm enabled
            if(lc_alg)
            {

                // variables to check for distance
                double min_dist_s_left = 100.;
                double min_dist_s_right = 100.;

                // variables to store the minimum speed of cars in front of us in other lanes
                double min_speed_left_lane = 50.;
                double min_speed_right_lane = 50.;

                // check if left lane is blocked:
                // - find minimum distance to cars in left lane
                // - find minimum speed of cars in front of us in left lane
                for(int i = 0; i < leftcars.size(); i++)
                {
                    double check_car_s = sensor_fusion[leftcars[i]][5];
                    double vx = sensor_fusion[leftcars[i]][3];
                    double vy = sensor_fusion[leftcars[i]][4];
                    double check_speed = sqrt(vx*vx + vy*vy);
                    double dist_to_car = abs(check_car_s - car_s);

                    // if using previos points can project s value outward some time
                    check_car_s += ((double)prev_size * 0.02 * check_speed);

                    // if distance is below min distance, set to min distance
                    if (dist_to_car < min_dist_s_left)
                    {
                        // exclude cars that are behind us with lower speed
                        if((check_speed + 5.) > ref_vel || check_car_s > (car_s - 10.))
                        {
                            min_dist_s_left = dist_to_car;
                        }
                    }
                    // if car is in front of us with distance up to 60 and speed is below minimum, set to min speed
                    if((check_car_s > car_s) && (dist_to_car < 60.) && (check_speed < min_speed_left_lane))
                    {
                        min_speed_left_lane = check_speed;
                    }
                }

                // check if right lane is blocked:
                // - find minimum distance to cars in right lane
                // - find minimum speed of cars in front of us in right lane
                for(int i = 0; i < rightcars.size(); i++)
                {
                    double check_car_s = sensor_fusion[rightcars[i]][5];
                    double vx = sensor_fusion[rightcars[i]][3];
                    double vy = sensor_fusion[rightcars[i]][4];
                    double check_speed = sqrt(vx*vx + vy*vy);
                    double dist_to_car = abs(check_car_s - car_s);

                    // if using previos points can project s value outward some time
                    check_car_s += ((double)prev_size * 0.02 * check_speed);

                    // if distance is below min distance, set to min distance
                    if (dist_to_car < min_dist_s_right)
                    {
                        // exclude cars that are behind us with lower speed
                        if((check_speed + 5.) > ref_vel || check_car_s > (car_s - 10.))
                        {
                            min_dist_s_right = dist_to_car;
                        }
                    }
                    // if car is in front of us with distance up to 60 and speed is below minimum, set to min speed
                    if((check_car_s > car_s) && (dist_to_car < 60.) && (check_speed < min_speed_right_lane))
                    {
                        min_speed_right_lane = check_speed;
                    }
                }

                // std::cout << "min_dist_s_left: " << min_dist_s_left;
                // std::cout << " min_dist_s_right: " << min_dist_s_right << endl;

                // if other cars are at safe distance and car is not in border lane enable lane change
                if (min_dist_s_left > (30 * 49.5 / ref_vel) && (lane >= 1))
                {
                    std::cout << "Left lane is free. Min distance: " << min_dist_s_left;
                    std::cout << " Min speed in left lane: " << min_speed_left_lane << endl;
                    change_left = true;
                }
                if (min_dist_s_right > 30 * 49.5 / ref_vel && (lane <= 1))
                {
                    std::cout << "Right lane is free. Min distance: " << min_dist_s_right;
                    std::cout << " Min speed in right lane: " << min_speed_right_lane << endl;
                    change_right = true;
                }

                // if both lanes are free, compare speeds of cars driving ahead of us
                // and change into faster lane, if faster than our lane
                if (change_left && change_right && (ref_vel < (min(min_speed_left_lane, min_speed_right_lane))))
                {
                    std::cout << "Both lanes are free." << endl;

                    if(min_speed_left_lane >= min_speed_right_lane)
                    {
                        // change lane to left and output msg
                        lane -= 1;
                        std::cout << "Changing lanes to the left." << endl;
                        // reset lane changing algorithm
                        lc_alg = false;
                    }

                    else if(min_speed_left_lane < min_speed_right_lane)
                    {
                        // change lane to right and output msg
                        lane += 1;
                        std::cout << "Changing lanes to the right." << endl;
                        // reset lane changing algorithm
                        lc_alg = false;
                    }
                }

                // if only one lane is free change to that, ...
                else
                {

                    // if speed is faster in the free lane and you are not already in the border left lane
                    if(change_left && (ref_vel < min_speed_left_lane))
                    {

                        // change lane to left and output msg
                        lane -= 1;
                        std::cout << "Changing lanes to the left." << endl;

                        // reset lane changing algorithm
                        lc_alg = false;

                    }

                    // if speed is faster in the free lane and you are not already in the border right lane
                    else if(change_right && (ref_vel < min_speed_right_lane))
                    {

                        // change lane to right and output msg
                        lane += 1;
                        std::cout << "Changing lanes to the right." << endl;

                        // reset lane changing algorithm
                        lc_alg = false;

                    }
                }

            }

            // speed up, if no car in front
            if(ref_vel < 49.5 && (!too_close))
            {
                std::cout << "Speeding up. Target speed: " << ref_vel << endl;
                ref_vel += .224;
            }

            // create a list of widely spread (x,y) waypoints, evenly spread at 30m
            // later we will interpolate these waypoints with a spline and fill it in with more points that control spline
            vector<double> ptsx;
            vector<double> ptsy;

            // reference x, y, yaw states
            // either we will reference the starting point as where the car is or at the previous paths end point
            double ref_x = car_x;
            double ref_y = car_y;
            double ref_yaw = deg2rad(car_yaw);

            // if previous path is almost empty, use the car as starting reference
            if(prev_size < 2)
            {
                // use two points that make the path tangent to the car
                double prev_car_x = car_x - cos(car_yaw);
                double prev_car_y = car_y - sin(car_yaw);

                ptsx.push_back(prev_car_x);
                ptsx.push_back(car_x);

                ptsy.push_back(prev_car_y);
                ptsy.push_back(car_y);
            }

            // use the previous path's end point as starting reference
            else
            {
                // redefine reference state as previous path end point
                ref_x = previous_path_x[prev_size - 1];
                ref_y = previous_path_y[prev_size - 1];

                double ref_x_prev = previous_path_x[prev_size - 2];
                double ref_y_prev = previous_path_y[prev_size - 2];
                ref_yaw = atan2(ref_y - ref_y_prev, ref_x - ref_x_prev);

                // use two points that make the path tangent to the previous path's end point
                ptsx.push_back(ref_x_prev);
                ptsx.push_back(ref_x);

                ptsy.push_back(ref_y_prev);
                ptsy.push_back(ref_y);

            }

            // in freenet add evenly 30m spaced points ahead of the starting reference
            vector<double> next_wp0 = getXY(car_s + 30, (2 + 4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp1 = getXY(car_s + 60, (2 + 4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp2 = getXY(car_s + 90, (2 + 4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);

            ptsx.push_back(next_wp0[0]);
            ptsx.push_back(next_wp1[0]);
            ptsx.push_back(next_wp2[0]);

            ptsy.push_back(next_wp0[1]);
            ptsy.push_back(next_wp1[1]);
            ptsy.push_back(next_wp2[1]);

            for (int i = 0; i < ptsx.size(); i++)
            {
                // shift car reference angle to 0 degrees
                double shift_x = ptsx[i] - ref_x;
                double shift_y = ptsy[i] - ref_y;

                ptsx[i] = (shift_x * cos(0 - ref_yaw) - shift_y * sin(0 - ref_yaw));
                ptsy[i] = (shift_x * sin(0 - ref_yaw) + shift_y * cos(0 - ref_yaw));

            }

            // create a spline
            tk::spline s;

            // set (x,y) points to the spline
            s.set_points(ptsx, ptsy);

            // define the actual (x,y) points we will use for the planner
            vector<double> next_x_vals;
            vector<double> next_y_vals;

            // start with all of the previous path points from last time
            for (int i = 0; i < previous_path_x.size(); i++)
            {
                next_x_vals.push_back(previous_path_x[i]);
                next_y_vals.push_back(previous_path_y[i]);
            }

            // calculate how to break up spline points, so that we travel at our desired reference velocity
            double target_x = 30.0;
            double target_y = s(target_x);
            double target_dist = sqrt(target_x * target_x + target_y * target_y);

            double x_add_on = 0;

            // fill up the rest of our path planner after filling it with previous points, here we will always output 50 points
            for (int i = 1; i <= 50 - previous_path_x.size(); i++)
            {
                double N = (target_dist / (0.02 * ref_vel / 2.24));
                double x_point = x_add_on + (target_x / N);
                double y_point = s(x_point);

                x_add_on = x_point;

                double x_ref = x_point;
                double y_ref = y_point;

                // rotate back to normal after rotating it earlier
                x_point = (x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw));
                y_point = (x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw));

                x_point += ref_x;
                y_point += ref_y;

                next_x_vals.push_back(x_point);
                next_y_vals.push_back(y_point);

            }

            msgJson["next_x"] = next_x_vals;
            msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);

        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
