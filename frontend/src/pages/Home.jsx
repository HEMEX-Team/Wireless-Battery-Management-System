import { Upload, Battery, Radio, Zap, CheckCircle } from "lucide-react";
import { Link } from "react-router-dom";
import { useAuth } from "../context/AuthContext";


export default function HomePage() {
  const { isAuthenticated } = useAuth();

  const features = [
    {
      icon: <Radio className="w-8 h-8 text-blue-500" />, 
      title: "Wireless Monitoring",
      description:
        "Monitor battery performance in real-time without the need for wired connections.",
    },
    {
      icon: <Battery className="w-8 h-8 text-green-500" />, 
      title: "Smart Battery Health",
      description:
        "Track voltage, temperature, and state-of-charge with intelligent algorithms.",
    },
    {
      icon: <Zap className="w-8 h-8 text-yellow-500" />, 
      title: "Efficient Energy Management",
      description:
        "Balance cells automatically to improve efficiency and extend battery life.",
    },
    {
      icon: <Upload className="w-8 h-8 text-purple-500" />, 
      title: "Data Logging & Insights",
      description:
        "Store and analyze performance data to optimize future battery usage.",
    },
  ];

  const benefits = [
    {
      title: "Enhanced Safety",
      description:
        "Prevent overheating, overcharging, and potential failures with real-time alerts.",
    },
    {
      title: "Longer Battery Life",
      description:
        "Optimized balancing ensures batteries last longer and perform better.",
    },
    {
      title: "Cutting-Edge Research",
      description:
        "An innovative approach to battery management using wireless technology.",
    },
  ];

  return (
    <div className="min-h-screen bg-white overflow-x-hidden">
      {/* Hero Section */}
      <section className="relative overflow-hidden bg-gradient-to-b from-blue-50/70 via-white to-white">
        {/* Ambient background glows */}
        <div className="pointer-events-none absolute inset-0">
          <div className="absolute -top-24 -left-24 h-72 w-72 rounded-full bg-blue-300/30 blur-3xl" />
          <div className="absolute top-1/3 right-0 h-80 w-80 rounded-full bg-cyan-300/20 blur-3xl" />
          <div
            className="absolute inset-0 opacity-[0.04]"
            style={{
              backgroundImage:
                "linear-gradient(#0f172a 1px, transparent 1px), linear-gradient(90deg, #0f172a 1px, transparent 1px)",
              backgroundSize: "44px 44px",
            }}
          />
        </div>

        <div className="relative max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-16 sm:py-20 lg:py-28">
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-12 lg:gap-10 items-center">
            {/* Left - Copy */}
            <div className="text-center lg:text-left">
              <span className="inline-flex items-center gap-2 rounded-full border border-blue-200 bg-blue-50 px-4 py-1.5 text-xs sm:text-sm font-medium text-blue-700">
                <span className="relative flex h-2 w-2">
                  <span className="absolute inline-flex h-full w-full animate-ping rounded-full bg-cyan-500 opacity-75" />
                  <span className="relative inline-flex h-2 w-2 rounded-full bg-cyan-500" />
                </span>
                Graduation Project
              </span>

              <h1 className="mt-6 text-4xl sm:text-5xl lg:text-6xl font-bold tracking-tight leading-[1.1] text-gray-900 break-words">
                Wireless{" "}
                <span className="bg-gradient-to-r from-blue-600 via-cyan-500 to-blue-500 bg-clip-text text-transparent">
                  Battery Management
                </span>{" "}
                System
              </h1>

              <p className="mt-6 text-base sm:text-lg text-gray-600 max-w-xl mx-auto lg:mx-0">
                A next-generation BMS where wireless slave nodes stream live cell
                data to a master controller — delivering safer, smarter, and more
                efficient battery monitoring in real time.
              </p>

              <div className="mt-9 flex flex-col sm:flex-row gap-4 justify-center lg:justify-start items-center">
                <Link to={isAuthenticated ? "/dashboard" : "/login"} className="w-full sm:w-auto">
                  <button className="w-full px-8 py-3.5 bg-gradient-to-r from-blue-600 to-cyan-500 text-white font-semibold rounded-xl shadow-lg shadow-blue-500/25 hover:from-blue-500 hover:to-cyan-400 focus:outline-none focus:ring-2 focus:ring-cyan-500 focus:ring-offset-2 transition duration-200">
                    View Prototype
                  </button>
                </Link>
                <Link to="/documentation" className="w-full sm:w-auto">
                  <button className="w-full px-8 py-3.5 bg-white text-gray-700 font-semibold rounded-xl border border-gray-300 hover:bg-gray-50 focus:outline-none focus:ring-2 focus:ring-gray-400 focus:ring-offset-2 transition duration-200">
                    Learn More
                  </button>
                </Link>
              </div>

              <div className="mt-10 flex flex-wrap justify-center lg:justify-start gap-x-8 gap-y-3 text-sm text-gray-500">
                <div className="flex items-center gap-2">
                  <Radio className="w-4 h-4 text-cyan-500" /> Wireless telemetry
                </div>
                <div className="flex items-center gap-2">
                  <Zap className="w-4 h-4 text-yellow-500" /> Real-time balancing
                </div>
                <div className="flex items-center gap-2">
                  <Battery className="w-4 h-4 text-green-500" /> Live cell health
                </div>
              </div>
            </div>

            {/* Right - Animated network diagram */}
            <div className="relative">
              <NetworkAnimation />
            </div>
          </div>
        </div>
      </section>

      {/* Features Section */}
      <section className="py-16 sm:py-20 px-4 sm:px-6 lg:px-8 bg-white">
        <div className="max-w-7xl mx-auto">
          <div className="text-center mb-12 sm:mb-16">
            <h2 className="text-2xl sm:text-3xl md:text-4xl font-bold text-gray-900 mb-4 break-words">
              Key Features of the Wireless BMS
            </h2>
            <p className="text-base sm:text-lg md:text-xl text-gray-600 max-w-3xl mx-auto px-2">
              Innovative technology designed to improve battery safety, performance,
              and monitoring.
            </p>
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-8">
            {features.map((feature, index) => (
              <div key={index} className="text-center">
                <div className="bg-gray-50 rounded-2xl p-4 w-16 h-16 mx-auto mb-6 flex items-center justify-center">
                  {feature.icon}
                </div>
                <h3 className="text-lg sm:text-xl font-semibold text-gray-900 mb-4 break-words">
                  {feature.title}
                </h3>
                <p className="text-gray-600 leading-relaxed break-words">
                  {feature.description}
                </p>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* Why Choose Section */}
      <section className="py-16 sm:py-20 px-4 sm:px-6 lg:px-8 bg-gray-50">
        <div className="max-w-7xl mx-auto">
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-10 lg:gap-16 items-center">
            {/* Left Side - Benefits */}
            <div>
              <h2 className="text-2xl sm:text-3xl md:text-4xl font-bold text-gray-900 mb-8 sm:mb-12 break-words">
                Why This Project Matters
              </h2>

              <div className="space-y-6 sm:space-y-8">
                {benefits.map((benefit, index) => (
                  <div key={index} className="flex items-start">
                    <div className="flex-shrink-0 mr-4">
                      <CheckCircle className="w-6 h-6 text-green-500" />
                    </div>
                    <div className="min-w-0">
                      <h3 className="text-lg sm:text-xl font-semibold text-gray-900 mb-2 break-words">
                        {benefit.title}
                      </h3>
                      <p className="text-gray-600 leading-relaxed break-words">
                        {benefit.description}
                      </p>
                    </div>
                  </div>
                ))}
              </div>
            </div>

            {/* Right Side - CTA */}
            <div className="bg-white rounded-2xl p-6 sm:p-8 shadow-lg border border-gray-200">
              <h3 className="text-xl sm:text-2xl font-bold text-gray-900 mb-4 text-center break-words">
                Explore the Project
              </h3>
              <p className="text-gray-600 text-center mb-6 sm:mb-8">
                A step forward in safe and efficient energy storage systems.
              </p>

              <div className="text-center">
                <Link to="/documentation" className="block">
                  <button className="w-full px-8 py-4 bg-blue-600 text-white font-semibold rounded-lg hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:ring-offset-2 transition duration-200 mb-4">
                    View Documentation
                  </button>
                </Link>

                <p className="text-sm text-gray-500">
                  Graduation Project • Wireless Battery Management System
                </p>
              </div>
            </div>
          </div>
        </div>
      </section>

      {/* Footer */}
      <footer className="bg-gray-900 text-white py-10 sm:py-12 px-4 sm:px-6 lg:px-8">
        <div className="max-w-7xl mx-auto text-center">
          <h3 className="text-lg sm:text-xl font-bold text-blue-400 mb-4 break-words">
            Wireless BMS Project
          </h3>
          <p className="text-sm sm:text-base text-gray-400 mb-6 max-w-2xl mx-auto break-words">
            A graduation project focused on advancing battery safety and performance
            through wireless communication and smart management.
          </p>
          <div className="flex flex-wrap justify-center gap-x-6 gap-y-3 sm:gap-x-8 text-sm text-gray-400">
            <a href="#" className="hover:text-white transition duration-200">
              Project Report
            </a>
            <a href="#" className="hover:text-white transition duration-200">
              Prototype Demo
            </a>
            <a href="#" className="hover:text-white transition duration-200">
              Contact Me
            </a>
          </div>
        </div>
      </footer>
    </div>
  );
}

/* ----------------------------------------------------------------------------
   Animated network diagram:  Slave nodes  ->  Master controller  ->  Users
   Pure inline SVG + SMIL (animateMotion) so it works with the Tailwind CDN
   without any build config, and scales perfectly on mobile.
---------------------------------------------------------------------------- */
function NetworkAnimation() {
  // Slave node vertical positions (box top-left y); shared geometry.
  const slaves = [
    { y: 30, label: "SLAVE 1", masterY: 168 },
    { y: 148, label: "SLAVE 2", masterY: 190 },
    { y: 266, label: "SLAVE 3", masterY: 212 },
  ];

  return (
    <div className="mx-auto w-full max-w-[520px] animate-[float_6s_ease-in-out_infinite]">
      <style>{`
        @keyframes float {
          0%, 100% { transform: translateY(0); }
          50% { transform: translateY(-10px); }
        }
      `}</style>
      <svg
        viewBox="0 0 480 380"
        className="w-full h-auto"
        role="img"
        aria-label="Animation of wireless slave nodes sending data packets to a master controller, which forwards them to users"
      >
        <defs>
          <filter id="glow" x="-50%" y="-50%" width="200%" height="200%">
            <feGaussianBlur stdDeviation="3" result="b" />
            <feMerge>
              <feMergeNode in="b" />
              <feMergeNode in="SourceGraphic" />
            </feMerge>
          </filter>
          <linearGradient id="nodeFill" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#ffffff" stopOpacity="1" />
            <stop offset="100%" stopColor="#eef2f7" stopOpacity="1" />
          </linearGradient>
          <radialGradient id="masterGlow" cx="50%" cy="50%" r="50%">
            <stop offset="0%" stopColor="#06b6d4" stopOpacity="0.22" />
            <stop offset="100%" stopColor="#06b6d4" stopOpacity="0" />
          </radialGradient>
          <filter id="nodeShadow" x="-30%" y="-30%" width="160%" height="160%">
            <feDropShadow dx="0" dy="4" stdDeviation="5" floodColor="#1e3a8a" floodOpacity="0.12" />
          </filter>
        </defs>

        {/* Connection lines */}
        {slaves.map((s, i) => {
          const startY = s.y + 35;
          return (
            <path
              key={`line-${i}`}
              d={`M118,${startY} C168,${startY} 178,${s.masterY} 214,${s.masterY}`}
              fill="none"
              stroke="rgba(100,116,139,0.45)"
              strokeWidth="1.5"
            />
          );
        })}
        <path
          d="M312,190 L372,190"
          fill="none"
          stroke="rgba(100,116,139,0.45)"
          strokeWidth="1.5"
        />

        {/* Packets: slaves -> master (sky blue) */}
        {slaves.map((s, i) => {
          const startY = s.y + 35;
          const d = `M118,${startY} C168,${startY} 178,${s.masterY} 214,${s.masterY}`;
          return [0, 1.1].map((delay, j) => (
            <circle key={`pk-${i}-${j}`} r="4.5" fill="#0ea5e9" filter="url(#glow)">
              <animateMotion
                dur="2.2s"
                begin={`${i * 0.4 + delay}s`}
                repeatCount="indefinite"
                path={d}
                keyPoints="0;1"
                keyTimes="0;1"
                calcMode="spline"
                keySplines="0.4 0 0.4 1"
              />
              <animate
                attributeName="opacity"
                values="0;1;1;0"
                keyTimes="0;0.1;0.85;1"
                dur="2.2s"
                begin={`${i * 0.4 + delay}s`}
                repeatCount="indefinite"
              />
            </circle>
          ));
        })}

        {/* Packets: master -> users (emerald) */}
        {[0, 0.9, 1.8].map((delay, i) => (
          <circle key={`mu-${i}`} r="4.5" fill="#10b981" filter="url(#glow)">
            <animateMotion
              dur="1.6s"
              begin={`${delay}s`}
              repeatCount="indefinite"
              path="M312,190 L372,190"
            />
            <animate
              attributeName="opacity"
              values="0;1;1;0"
              keyTimes="0;0.15;0.8;1"
              dur="1.6s"
              begin={`${delay}s`}
              repeatCount="indefinite"
            />
          </circle>
        ))}

        {/* Slave nodes */}
        {slaves.map((s, i) => (
          <g key={`slave-${i}`}>
            <rect
              x="22"
              y={s.y}
              width="96"
              height="70"
              rx="14"
              fill="url(#nodeFill)"
              stroke="#0ea5e9"
              strokeWidth="1.5"
              filter="url(#nodeShadow)"
            />
            {/* battery glyph */}
            <rect x="44" y={s.y + 18} width="40" height="20" rx="3" fill="none" stroke="#0ea5e9" strokeWidth="2" />
            <rect x="84" y={s.y + 23} width="4" height="10" rx="1" fill="#0ea5e9" />
            <rect x="47" y={s.y + 21} width="22" height="14" rx="1" fill="#0ea5e9">
              <animate
                attributeName="width"
                values="8;30;8"
                dur="3s"
                begin={`${i * 0.5}s`}
                repeatCount="indefinite"
              />
            </rect>
            <text x="70" y={s.y + 56} textAnchor="middle" fill="#475569" fontSize="11" fontWeight="600" fontFamily="ui-sans-serif, system-ui">
              {s.label}
            </text>
          </g>
        ))}

        {/* Master controller */}
        <circle cx="263" cy="190" r="70" fill="url(#masterGlow)">
          <animate attributeName="r" values="58;74;58" dur="3s" repeatCount="indefinite" />
        </circle>
        <rect
          x="214"
          y="150"
          width="98"
          height="80"
          rx="16"
          fill="url(#nodeFill)"
          stroke="#0891b2"
          strokeWidth="2"
          filter="url(#nodeShadow)"
        />
        {/* wifi / broadcast arcs */}
        <g stroke="#0891b2" strokeWidth="2.5" fill="none" strokeLinecap="round">
          <path d="M250,182 a18,18 0 0 1 26,0" />
          <path d="M244,176 a26,26 0 0 1 38,0" />
          <circle cx="263" cy="188" r="3" fill="#0891b2" stroke="none" />
        </g>
        <text x="263" y="216" textAnchor="middle" fill="#0f172a" fontSize="12" fontWeight="700" fontFamily="ui-sans-serif, system-ui">
          MASTER
        </text>

        {/* Users / dashboard */}
        <g>
          <rect
            x="372"
            y="152"
            width="86"
            height="76"
            rx="14"
            fill="url(#nodeFill)"
            stroke="#10b981"
            strokeWidth="1.5"
            filter="url(#nodeShadow)"
          />
          {/* monitor glyph */}
          <rect x="392" y="166" width="46" height="30" rx="3" fill="none" stroke="#10b981" strokeWidth="2" />
          <line x1="415" y1="196" x2="415" y2="204" stroke="#10b981" strokeWidth="2" />
          <line x1="405" y1="204" x2="425" y2="204" stroke="#10b981" strokeWidth="2" strokeLinecap="round" />
          <text x="415" y="220" textAnchor="middle" fill="#475569" fontSize="11" fontWeight="600" fontFamily="ui-sans-serif, system-ui">
            USERS
          </text>
        </g>
      </svg>
    </div>
  );
}
