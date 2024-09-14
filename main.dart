import 'dart:async';
import 'package:flutter/material.dart';
import 'package:google_maps_flutter/google_maps_flutter.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp();
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Urban Hotspots',
      home: const HomeScreen(),
    );
  }
}

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  final PageController _pageController = PageController();
  int _currentPage = 0;
  int _peopleCount = 0; // This will store the count of people

  @override
  void initState() {
    super.initState();
    _pageController.addListener(_updatePage);
  }

  void _updatePage() {
    if (_currentPage != _pageController.page!.round()) {
      setState(() {
        _currentPage = _pageController.page!.round();
      });
    }
  }

  @override
  void dispose() {
    _pageController.removeListener(_updatePage);
    _pageController.dispose();
    super.dispose();
  }

  // Method to update the count from ThirdPage and propagate changes to MapSample
  void updatePeopleCount(int count) {
    setState(() {
      _peopleCount = count;
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: PageView(
        controller: _pageController,
        physics: const AlwaysScrollableScrollPhysics(),
        children: <Widget>[
          const SplashScreen(),
          MapSample(
            peopleCount: _peopleCount,
            updatePeopleCount: updatePeopleCount,
          ), // Pass the count and the update function
          ThirdPage(
            initialCount: _peopleCount,
            onUpdateCount: updatePeopleCount,
          ), // Pass the initial count and the update function
        ],
      ),
      bottomSheet: Container(
        padding: const EdgeInsets.symmetric(vertical: 10),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: List.generate(
            3,
            (index) => buildDot(
                index: index), // Generating dots based on the number of pages
          ),
        ),
      ),
    );
  }

  Widget buildDot({required int index}) {
    return Container(
      height: 10,
      width: _currentPage == index ? 10 : 7, // Current page dot is larger
      margin: const EdgeInsets.symmetric(horizontal: 5),
      decoration: BoxDecoration(
        color: _currentPage == index ? Colors.black : Colors.grey,
        shape: BoxShape.circle,
      ),
    );
  }
}

class SplashScreen extends StatelessWidget {
  const SplashScreen({super.key});

  @override
  Widget build(BuildContext context) {
    // Access HomeScreen state to retrieve updatePeopleCount and peopleCount
    final _HomeScreenState homeState =
        context.findAncestorStateOfType<_HomeScreenState>()!;

    return Scaffold(
      body: Container(
        decoration: BoxDecoration(
          image: DecorationImage(
            image: AssetImage('assets/Urban.png'),
            fit: BoxFit.cover,
          ),
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.start,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            SizedBox(height: 48),
            Text(
              'Urban Hotspots',
              style: TextStyle(
                fontSize: 32,
                fontWeight: FontWeight.bold,
                color: Colors.white,
                fontFamily: 'PaytoneOne',
              ),
              textAlign: TextAlign.center,
            ),
            Spacer(),
            ElevatedButton(
              onPressed: () {
                Navigator.of(context).push(MaterialPageRoute(
                    builder: (_) => MapSample(
                          peopleCount: homeState
                              ._peopleCount, // Access the current people count
                          updatePeopleCount: homeState
                              .updatePeopleCount, // Access the update function
                        )));
              },
              child: Text('Continue to Heat-map',
                  style: TextStyle(color: Colors.black)),
              style: ElevatedButton.styleFrom(
                padding: EdgeInsets.symmetric(horizontal: 50, vertical: 20),
                textStyle: TextStyle(fontSize: 20),
                alignment: Alignment.center,
                backgroundColor: Colors.white,
                foregroundColor: Colors.black,
              ),
            ),
            Spacer(flex: 2),
          ],
        ),
      ),
      bottomSheet: Container(
        padding: const EdgeInsets.only(top: 10, bottom: 20),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              'By: Chandler Harrison, Eddie Armeriv, and Adarsh Ram',
              style: TextStyle(fontSize: 16, color: Colors.black),
              textAlign: TextAlign.center,
            ),
            SizedBox(height: 10),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: List.generate(
                  3, (index) => buildDot(index: index, context: context)),
            ),
          ],
        ),
      ),
    );
  }

  Widget buildDot({required int index, required BuildContext context}) {
    final _HomeScreenState homeState =
        context.findAncestorStateOfType<_HomeScreenState>()!;
    return Container(
      height: 10,
      width: homeState._currentPage == index ? 10 : 7,
      margin: const EdgeInsets.symmetric(horizontal: 5),
      decoration: BoxDecoration(
        color: homeState._currentPage == index ? Colors.black : Colors.grey,
        shape: BoxShape.circle,
      ),
    );
  }
}

class MapSample extends StatefulWidget {
  final int peopleCount;
  final Function(int) updatePeopleCount;

  const MapSample({
    super.key,
    required this.peopleCount,
    required this.updatePeopleCount,
  });

  @override
  State<MapSample> createState() => MapSampleState();
}

class MapSampleState extends State<MapSample> {
  final Completer<GoogleMapController> _controller = Completer();
  static final Map<String, Marker> _markers =
      {}; // Use a map to track markers by a unique key
  late DatabaseReference databaseReference;

  @override
  void initState() {
    super.initState();
    _initializeFirebase();
  }

  void _initializeFirebase() async {
    await Firebase.initializeApp();
    databaseReference = FirebaseDatabase.instance.ref();
    databaseReference.onValue.listen((DatabaseEvent event) {
      final data = event.snapshot.value as Map<dynamic, dynamic>?;
      if (data != null) {
        _updateMarkers(data);
      }
    });
  }

  void _updateMarkers(Map<dynamic, dynamic> data) {
    final lat = data['GPS']['Latitude'] as double;
    final lng = data['GPS']['Longitude'] as double;
    final temp = data['Environment']['Temperature'].toString();
    final humidity = data['Environment']['Humidity'].toString();
    String markerId = "marker_$lat,$lng";

    LatLng newPosition = LatLng(lat, lng);
    Marker newMarker = _createMarker(newPosition, lat, lng, temp,
        humidity); // Always create a new marker to check for updated color

    if (_markers.containsKey(markerId)) {
      // Update existing marker
      Marker updatedMarker = _markers[markerId]!.copyWith(
        positionParam: newPosition,
        infoWindowParam: InfoWindow(
          title: "Lat: $lat, Lng: $lng",
          snippet:
              "Temperature: $temp°C, Humidity: $humidity%, People: ${widget.peopleCount}",
        ),
        iconParam: newMarker
            .icon, // Ensure the icon color is updated based on new temperature
      );
      setState(() {
        _markers[markerId] = updatedMarker;
      });
    } else {
      // Add new marker if it does not exist
      setState(() {
        _markers[markerId] = newMarker;
      });
    }
    _gotoNewPosition(lat, lng);
  }

  Marker _createMarker(
      LatLng position, double lat, double lng, String temp, String humidity) {
    double temperature =
        double.tryParse(temp) ?? 0.0; // Parse temperature string to double
    BitmapDescriptor markerColor =
        BitmapDescriptor.defaultMarker; // Default color

    // Determine the color based on the temperature
    if (temperature >= 80.4) {
      markerColor =
          BitmapDescriptor.defaultMarkerWithHue(BitmapDescriptor.hueRed);
    } else if (temperature >= 78.7) {
      markerColor =
          BitmapDescriptor.defaultMarkerWithHue(BitmapDescriptor.hueOrange);
    } else if (temperature >= 77.0) {
      markerColor =
          BitmapDescriptor.defaultMarkerWithHue(BitmapDescriptor.hueYellow);
    } else if (temperature >= 75.3) {
      markerColor =
          BitmapDescriptor.defaultMarkerWithHue(BitmapDescriptor.hueGreen);
    } else if (temperature >= 73.6) {
      markerColor =
          BitmapDescriptor.defaultMarkerWithHue(BitmapDescriptor.hueCyan);
    } else if (temperature >= 71.9) {
      markerColor =
          BitmapDescriptor.defaultMarkerWithHue(BitmapDescriptor.hueBlue);
    }

    return Marker(
      markerId: MarkerId("marker_$lat,$lng"),
      position: position,
      infoWindow: InfoWindow(
        title: "Lat: $lat, Lng: $lng",
        snippet:
            "Temperature: $temp°C, Humidity: $humidity%, People: ${widget.peopleCount}",
      ),
      icon: markerColor,
    );
  }

  Future<void> _gotoNewPosition(double lat, double lng) async {
    final GoogleMapController mapController = await _controller.future;
    mapController.animateCamera(CameraUpdate.newCameraPosition(CameraPosition(
      target: LatLng(lat, lng),
      zoom: 13.0,
    )));
  }

  final GlobalKey<ScaffoldState> _scaffoldKey = GlobalKey<ScaffoldState>();

  Widget buildDrawer(BuildContext context) {
    return Drawer(
      child: ListView(
        padding: EdgeInsets.zero,
        children: [
          Padding(
            padding: EdgeInsets.only(
                top: MediaQuery.of(context).size.height *
                    0.1), // Extra padding above the title
            child: ListTile(
              title: Text('Temp Guide'),
              dense: true,
              onTap: () {}, // Add functionality if needed
            ),
          ),
          ListTile(
            leading: CircleAvatar(backgroundColor: Colors.red[800]),
            title: Text('>=80.4'),
            onTap: () {}, // Add functionality if needed
          ),
          ListTile(
            leading: CircleAvatar(backgroundColor: Colors.orange[800]),
            title: Text('>=78.7'),
            onTap: () {}, // Add functionality if needed
          ),
          ListTile(
            leading: CircleAvatar(backgroundColor: Colors.yellow),
            title: Text('>=77.0'),
            onTap: () {}, // Add functionality if needed
          ),
          ListTile(
            leading: CircleAvatar(backgroundColor: Colors.green[800]),
            title: Text('>=75.3'),
            onTap: () {}, // Add functionality if needed
          ),
          ListTile(
            leading: CircleAvatar(backgroundColor: Colors.cyan),
            title: Text('>=73.6'),
            onTap: () {}, // Add functionality if needed
          ),
          ListTile(
            leading: CircleAvatar(backgroundColor: Colors.blue[800]),
            title: Text('>=71.9'),
            onTap: () {}, // Add functionality if needed
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      key: _scaffoldKey, // Assign the GlobalKey to Scaffold
      endDrawer: buildDrawer(context), // Set the endDrawer property
      body: Stack(
        children: [
          GoogleMap(
            mapType: MapType.normal,
            initialCameraPosition: CameraPosition(
              target: LatLng(0, 0), // Initial camera position
              zoom: 17.0,
            ),
            markers: _markers.values.toSet(),
            onMapCreated: (GoogleMapController controller) {
              _controller.complete(controller);
            },
          ),
          Positioned(
            bottom: 20,
            left: 20,
            child: ElevatedButton(
              onPressed: () {
                Navigator.of(context).popUntil((route) => route.isFirst);
              },
              child: Text('Home', style: TextStyle(color: Colors.black)),
              style: ElevatedButton.styleFrom(
                backgroundColor: Colors.white, // Button background color
                foregroundColor: Colors.grey, // Text color
              ),
            ),
          ),
          Positioned(
            right: 20,
            top: MediaQuery.of(context).size.height / 2 -
                24, // Adjusted for the new size
            child: InkWell(
              onTap: () => _scaffoldKey.currentState!.openEndDrawer(),
              child: Container(
                width: 20, // Smaller width
                height: 80, // Smaller height
                decoration: BoxDecoration(
                  color: Colors.white,
                  borderRadius: BorderRadius.circular(8), // Rounded corners
                  boxShadow: [
                    BoxShadow(
                      color: Colors.black.withOpacity(0.2),
                      blurRadius: 4,
                      offset: Offset(0, 2), // Shadow direction: moving down
                    )
                  ],
                ),
                child: Icon(Icons.menu, color: Colors.grey),
                alignment: Alignment.center,
              ),
            ),
          ),
        ],
      ),
    );
  }

  void _changeZoom(bool zoomIn) async {
    final GoogleMapController controller = await _controller.future;
    final currentZoom = await controller.getZoomLevel();
    controller.animateCamera(
      CameraUpdate.zoomTo(zoomIn ? currentZoom + 1 : currentZoom - 1),
    );
  }
}

class ThirdPage extends StatefulWidget {
  final int initialCount;
  final Function(int) onUpdateCount;

  const ThirdPage(
      {super.key, required this.initialCount, required this.onUpdateCount});

  @override
  State<ThirdPage> createState() => _ThirdPageState();
}

class _ThirdPageState extends State<ThirdPage> {
  late int _count;
  late TextEditingController _controller;

  @override
  void initState() {
    super.initState();
    _count = widget.initialCount; // Initialize with the passed initial count
    _controller = TextEditingController(text: _count.toString());
  }

  void _updateCount(int newCount) {
    if (newCount >= 0) {
      setState(() {
        _count = newCount;
        _controller.text = _count.toString();
      });
      widget.onUpdateCount(
          _count); // Call the callback function to update the count in parent
    }
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: () => FocusScope.of(context).unfocus(),
      child: Scaffold(
        body: Column(
          mainAxisAlignment: MainAxisAlignment.start,
          crossAxisAlignment: CrossAxisAlignment.center,
          children: <Widget>[
            Padding(
              padding: const EdgeInsets.only(top: 60.0),
              child: Text(
                'People Counter',
                style: TextStyle(
                    fontSize: 32,
                    fontWeight: FontWeight.bold,
                    color: Colors.black),
                textAlign: TextAlign.center,
              ),
            ),
            Expanded(
              child: Center(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: <Widget>[
                    Text(
                      'Enter the number of people',
                      style: TextStyle(fontSize: 20),
                    ),
                    SizedBox(height: 10),
                    TextField(
                      controller: _controller,
                      keyboardType: TextInputType.number,
                      textInputAction: TextInputAction.done,
                      onSubmitted: (_) => FocusScope.of(context).unfocus(),
                      textAlign: TextAlign.center,
                      style:
                          TextStyle(fontSize: 24, fontWeight: FontWeight.bold),
                      decoration: InputDecoration(
                        border: OutlineInputBorder(),
                        hintText: '0',
                        suffixIcon: IconButton(
                          icon: Icon(Icons.clear),
                          onPressed: () {
                            _controller.clear();
                            _updateCount(0); // Reset count to 0 when cleared
                          },
                        ),
                      ),
                      onChanged: (value) {
                        int? newCount = int.tryParse(value);
                        if (newCount != null) {
                          _updateCount(newCount);
                        }
                      },
                    ),
                    SizedBox(height: 10),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: <Widget>[
                        ElevatedButton(
                          onPressed: () => _updateCount(_count + 1),
                          child: Text('+'),
                        ),
                        SizedBox(width: 20),
                        ElevatedButton(
                          onPressed: () => _updateCount(_count - 1),
                          child: Text('-'),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }
}
