#include <lux/communication/intraprocess/Node.hpp>
#include <lux/communication/intraprocess/Publisher.hpp>
#include <lux/communication/intraprocess/Subscriber.hpp>

int main()
{
	using namespace lux::communication::intraprocess;

	Node node("default node");
	Publisher<int> publisher("/whatever/topic", &node);
	Subscriber<int> subscriber(
		"/whatever/topic", &node,
		[](const std::shared_ptr<int>)
		{

		}
	);

	return 0;
}