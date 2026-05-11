import { Upload, Brain, BarChart3, Zap, Shield, Users, Clock, Globe } from 'lucide-react';

export default function FeaturesPage() {
  const features = [
    {
      icon: <Upload className="w-8 h-8 text-blue-500" />,
      title: "Smart File Upload",
      description: "Drag and drop your Excel files for instant analysis with automatic data detection."
    },
    {
      icon: <Brain className="w-8 h-8 text-green-500" />,
      title: "AI-Powered Analytics",
      description: "Ask questions in natural language and get intelligent responses with relevant insights."
    },
    {
      icon: <BarChart3 className="w-8 h-8 text-purple-500" />,
      title: "Interactive Visualizations",
      description: "Generate beautiful interactive charts that update in real-time as you explore."
    },
    {
      icon: <Zap className="w-8 h-8 text-yellow-500" />,
      title: "Smart Recommendations",
      description: "Get personalized suggestions for data exploration and business insights."
    },
    {
      icon: <Shield className="w-8 h-8 text-red-500" />,
      title: "Enterprise Security",
      description: "Your data is protected with enterprise-grade security and encryption."
    },
    {
      icon: <Users className="w-8 h-8 text-blue-600" />,
      title: "Team Collaboration",
      description: "Share insights and collaborate on analysis with built-in sharing features."
    },
    {
      icon: <Clock className="w-8 h-8 text-orange-500" />,
      title: "Automated Reports",
      description: "Schedule automated reports and get insights delivered to your inbox."
    },
    {
      icon: <Globe className="w-8 h-8 text-teal-500" />,
      title: "API Integration",
      description: "Connect with your existing tools through our robust API and integrations."
    }
  ];

  return (
    <div className="min-h-screen bg-white overflow-x-hidden">

      {/* Hero Section */}
      <section className="py-16 sm:py-20 px-4 sm:px-6 lg:px-8">
        <div className="max-w-4xl mx-auto text-center">
          <h1 className="text-3xl sm:text-4xl md:text-5xl font-bold text-gray-900 mb-6 break-words">
            Powerful Features for Data Analysis
          </h1>
          <p className="text-base sm:text-lg md:text-xl text-gray-600 max-w-3xl mx-auto px-2">
            Transform your Excel data into actionable insights with our comprehensive suite of AI-powered tools.
          </p>
        </div>
      </section>

      {/* Features Grid */}
      <section className="py-12 sm:py-16 px-4 sm:px-6 lg:px-8 bg-gray-50">
        <div className="max-w-7xl mx-auto">
          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-6">
            {features.map((feature, index) => (
              <div key={index} className="bg-white rounded-lg p-6 shadow-sm border border-gray-200 hover:shadow-md transition-shadow duration-200">
                <div className="mb-4">
                  {feature.icon}
                </div>
                <h3 className="text-lg font-semibold text-gray-900 mb-3 break-words">
                  {feature.title}
                </h3>
                <p className="text-gray-600 text-sm leading-relaxed break-words">
                  {feature.description}
                </p>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* CTA Section */}
      <section className="py-16 sm:py-20 px-4 sm:px-6 lg:px-8 bg-white">
        <div className="max-w-4xl mx-auto text-center">
          <h2 className="text-2xl sm:text-3xl md:text-4xl font-bold text-gray-900 mb-6 break-words">
            Ready to Get Started?
          </h2>
          <p className="text-base sm:text-lg md:text-xl text-gray-600 mb-8 sm:mb-10 max-w-2xl mx-auto px-2">
            Join thousands of professionals using DataInsight.io to unlock their data's potential.
          </p>
          
          <div className="flex flex-col sm:flex-row gap-4 justify-center items-center">
            <button className="px-8 py-3 bg-blue-600 text-white font-semibold rounded-lg hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:ring-offset-2 transition duration-200">
              Start Free Trial
            </button>
            <button className="px-8 py-3 bg-white text-gray-700 font-semibold rounded-lg border border-gray-300 hover:bg-gray-50 focus:outline-none focus:ring-2 focus:ring-gray-500 focus:ring-offset-2 transition duration-200">
              Schedule Demo
            </button>
          </div>
          
          <p className="text-sm text-gray-500 mt-6">
            No credit card required • 14-day free trial
          </p>
        </div>
      </section>

    </div>
  );
}