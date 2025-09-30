#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "ob/book.hpp"
#include "ob/order.hpp"


namespace py = pybind11;

PYBIND11_MODULE(obsim, m) {
  py::enum_<Side>(m, "Side").value("Buy", Side::Buy).value("Sell", Side::Sell);
  py::enum_<Type>(m, "Type").value("Limit", Type::Limit).value("Market", Type::Market);
  py::enum_<TIF>(m, "TIF")
    .value("Day", TIF::Day).value("IOC", TIF::IOC)
    .value("FOK", TIF::FOK).value("GTC", TIF::GTC);

  py::class_<Order>(m, "Order")
    .def(py::init<>())
    .def_readwrite("id", &Order::id)
    .def_readwrite("side", &Order::side)
    .def_readwrite("type", &Order::type)
    .def_readwrite("tif", &Order::tif)
    .def_readwrite("px", &Order::px)
    .def_readwrite("qty", &Order::qty)
    .def_readwrite("ts_ns", &Order::ts_ns)
    .def_readwrite("post_only", &Order::post_only);

  py::class_<LevelView>(m, "LevelView")
    .def_readonly("px", &LevelView::px)
    .def_readonly("qty", &LevelView::qty)
    .def_readonly("orders", &LevelView::orders);

  py::class_<OrderBook>(m, "OrderBook")
    .def(py::init<std::string, int64_t>())
    .def("add", &OrderBook::add)
    .def("cancel", &OrderBook::cancel)
    .def("replace", &OrderBook::replace)
    .def("bids", &OrderBook::bids)
    .def("asks", &OrderBook::asks);
}
